// QA-owned data validation for the reel fight tables (T-007): Project.Fishing.Fight.QA.Data.*
// Written by the qa-engineer from docs/specs/reel-fight-rules.md and the row contracts in Fishing/FishFightTypes.h (black-box).
// Every shipped row of DT_Gear, DT_FightPattern and DT_FishFight is checked against rules restated here (not only against the
// rows' own Validate()), bad rows are fed through the real import path (CSV/JSON text) and must be refused, and the tables are
// cross-checked with DT_Fishing (cast distances, line length) and the fish tables (patterns, stats).

#include "FightQATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include <limits>

namespace LureFightQA
{
	namespace DataQA
	{
		/** Row names of a CSV (first cell of every data line), exactly as written. */
		inline TArray<FString> CsvRowNames(const FString& Csv)
		{
			TArray<FString> Lines;
			Csv.ParseIntoArrayLines(Lines, true);
			TArray<FString> Names;
			for (int32 Line = 1; Line < Lines.Num(); ++Line)
			{
				FString Name;
				Lines[Line].Split(TEXT(","), &Name, nullptr);
				Names.Add(Name.TrimStartAndEnd());
			}
			return Names;
		}

		/** "Name" of every object in a JSON array source, exactly as written. */
		inline TArray<FString> JsonRowNames(FAutomationTestBase& Test, const FString& Json)
		{
			TArray<TSharedPtr<FJsonValue>> Rows;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			TArray<FString> Names;
			if (!Test.TestTrue(TEXT("the JSON source parses as an array"), FJsonSerializer::Deserialize(Reader, Rows)))
			{
				return Names;
			}
			for (const TSharedPtr<FJsonValue>& Row : Rows)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				FString Name;
				if (Row.IsValid() && Row->TryGetObject(Object) && Object && (*Object)->TryGetStringField(TEXT("Name"), Name))
				{
					Names.Add(Name);
				}
				else
				{
					Test.AddError(TEXT("a JSON row has no Name"));
				}
			}
			return Names;
		}

		/** Names are unique the way FName compares them (case-insensitive). */
		inline void TestUniqueNames(FAutomationTestBase& Test, const TArray<FString>& Names, const TCHAR* Source)
		{
			TSet<FString> Seen;
			for (const FString& Name : Names)
			{
				const FString Key = Name.ToLower();
				Test.TestFalse(FString::Printf(TEXT("%s: row name '%s' is unique (FName ignores case)"), Source, *Name), Seen.Contains(Key));
				Test.TestFalse(FString::Printf(TEXT("%s: a row name is not empty"), Source), Name.IsEmpty() || Name.Equals(TEXT("None"), ESearchCase::IgnoreCase));
				Seen.Add(Key);
			}
		}

		inline bool FiniteNonNegative(std::initializer_list<float> Values)
		{
			for (const float Value : Values)
			{
				if (!FMath::IsFinite(Value) || Value < 0.f)
				{
					return false;
				}
			}
			return true;
		}

		inline FString GearCsv(const TCHAR* Rows)
		{
			return FString(TEXT("Name,Slot,DisplayName,Price,RodPower,ReelSpeed,Drag,CastDistanceMultiplier,LineStrength,SpoolLength,HookSecurity,BaitTag,Luck,DevComment\n")) + Rows;
		}

		/** A valid move as JSON; Overrides replace field values (every key is written once). */
		inline FString MoveJson(const TCHAR* Id, const TMap<FString, FString>& Overrides = TMap<FString, FString>())
		{
			static const TCHAR* const Fields[][2] = { { TEXT("Weight"), TEXT("1") }, { TEXT("AggressionWeight"), TEXT("0") }, { TEXT("DurationMin"), TEXT("1") },
				{ TEXT("DurationMax"), TEXT("2") }, { TEXT("Pull"), TEXT("1") }, { TEXT("Speed"), TEXT("1") }, { TEXT("Away"), TEXT("0.5") }, { TEXT("Side"), TEXT("0") },
				{ TEXT("RandomSide"), TEXT("false") }, { TEXT("Down"), TEXT("0") }, { TEXT("Rest"), TEXT("false") } };
			FString Out = FString::Printf(TEXT("{ \"Id\": \"%s\", \"Label\": \"%s\""), Id, Id);
			for (const auto& Field : Fields)
			{
				const FString* Override = Overrides.Find(Field[0]);
				Out += FString::Printf(TEXT(", \"%s\": %s"), Field[0], Override ? **Override : Field[1]);
			}
			return Out + TEXT(" }");
		}
	}

	// =================================================================================================================
	// DT_Gear
	// =================================================================================================================

	/** Every shipped DT_Gear row, against the rules restated from the spec and the FLureGearRow contract. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQADataGearEveryRow, "Project.Fishing.Fight.QA.Data.GearEveryRow", Flags)
	bool FLureFightQADataGearEveryRow::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		DataQA::TestUniqueNames(*this, DataQA::CsvRowNames(Data.GearCsv), TEXT("DT_Gear.csv"));
		const FLureGearLoadout& Defaults = GetDefault<ULureFishingSettings>()->DefaultLoadout;
		TMap<ELureGearSlot, int32> PerSlot;
		for (const TPair<FName, uint8*>& Pair : Data.Gear->GetRowMap())
		{
			const FLureGearRow& Row = *reinterpret_cast<const FLureGearRow*>(Pair.Value);
			const FString Name = Pair.Key.ToString();
			PerSlot.FindOrAdd(Row.Slot)++;
			const bool bStarter = Defaults.Get(Row.Slot) == Pair.Key;
			TestFalse(Name + TEXT(": has a display name"), Row.DisplayName.IsEmpty());
			TestTrue(Name + TEXT(": price >= 0"), Row.Price >= 0);
			TestTrue(FString::Printf(TEXT("%s: the starter kit is free and shop items cost something (price %d)"), *Name, Row.Price), bStarter ? Row.Price == 0 : Row.Price > 0);
			switch (Row.Slot)
			{
			case ELureGearSlot::Rod:
				TestTrue(Name + TEXT(": rod power, reel speed and cast multiplier finite and > 0, drag finite and >= 0"),
					DataQA::FiniteNonNegative({ Row.RodPower, Row.ReelSpeed, Row.Drag, Row.CastDistanceMultiplier }) && Row.RodPower > 0.f && Row.ReelSpeed > 0.f && Row.CastDistanceMultiplier > 0.f);
				TestTrue(FString::Printf(TEXT("%s: cast multiplier in a sane range (%.2f)"), *Name, Row.CastDistanceMultiplier), Row.CastDistanceMultiplier >= 0.5f && Row.CastDistanceMultiplier <= 2.f);
				TestTrue(FString::Printf(TEXT("%s: letting it run eases the rod (drag %.1f < power %.1f)"), *Name, Row.Drag, Row.RodPower), Row.Drag < Row.RodPower);
				break;
			case ELureGearSlot::Line:
				TestTrue(Name + TEXT(": line strength and spool finite and > 0"), DataQA::FiniteNonNegative({ Row.LineStrength, Row.SpoolLength }) && Row.LineStrength > 0.f && Row.SpoolLength > 0.f);
				break;
			case ELureGearSlot::Hook:
				TestTrue(Name + TEXT(": hook security finite and > 0, luck finite and >= 0"), DataQA::FiniteNonNegative({ Row.HookSecurity, Row.Luck }) && Row.HookSecurity > 0.f);
				TestTrue(FString::Printf(TEXT("%s: bait '%s' is a registered Bait.* tag"), *Name, *Row.BaitTag.ToString()), Row.BaitTag.IsValid() && Row.BaitTag.MatchesTag(Tag(TEXT("Bait"))));
				break;
			default:
				AddError(Name + TEXT(": unknown slot"));
				break;
			}
			FString Problem;
			TestTrue(Name + TEXT(": FLureGearRow::Validate agrees: ") + Problem, Row.Validate(Problem));
			TestTrue(Name + TEXT(": can be equipped in its slot"), FLureGear::CanEquip(Data.Gear.Get(), Row.Slot, Pair.Key));
			for (const ELureGearSlot Other : { ELureGearSlot::Rod, ELureGearSlot::Line, ELureGearSlot::Hook })
			{
				if (Other != Row.Slot)
				{
					TestFalse(Name + TEXT(": can't be equipped in another slot"), FLureGear::CanEquip(Data.Gear.Get(), Other, Pair.Key));
				}
			}
		}
		for (const ELureGearSlot Slot : { ELureGearSlot::Rod, ELureGearSlot::Line, ELureGearSlot::Hook })
		{
			const FString SlotName = StaticEnum<ELureGearSlot>()->GetNameStringByValue(static_cast<int64>(Slot));
			TestTrue(FString::Printf(TEXT("%s: at least a starter and an upgrade"), *SlotName), PerSlot.FindRef(Slot) >= 2);
			const FLureGearRow* Starter = Data.Item(Defaults.Get(Slot));
			TestTrue(FString::Printf(TEXT("%s: the default loadout's '%s' is a %s row"), *SlotName, *Defaults.Get(Slot).ToString(), *SlotName), Starter && Starter->Slot == Slot);
		}
		const FLureGearStats Starter = Data.Starter();
		TestFalse(TEXT("the default loadout resolves without built-in items"), Starter.bUsedFallback);
		return true;
	}

	/** Bad DT_Gear rows typed into the CSV are refused: never equipped, the loadout falls back to the starter item of that slot. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQADataGearBadRows, "Project.Fishing.Fight.QA.Data.GearBadRowsRejected", Flags)
	bool FLureFightQADataGearBadRows::RunTest(const FString& Parameters)
	{
		const FString Csv = DataQA::GearCsv(
			TEXT("Rod_Ok,Rod,Ok,10,8,120,5,1.0,0,0,0,None,0,control\n")
			TEXT("Rod_NegPrice,Rod,Bad,-1,8,120,5,1.0,0,0,0,None,0,qa\n")
			TEXT("Rod_NoReel,Rod,Bad,10,8,0,5,1.0,0,0,0,None,0,qa\n")
			TEXT("Rod_NegReel,Rod,Bad,10,8,-120,5,1.0,0,0,0,None,0,qa\n")
			TEXT("Rod_NegDrag,Rod,Bad,10,8,120,-1,1.0,0,0,0,None,0,qa\n")
			TEXT("Rod_NoCast,Rod,Bad,10,8,120,5,0,0,0,0,None,0,qa\n")
			TEXT("Line_Ok,Line,Ok,10,0,0,0,0,10,4000,0,None,0,control\n")
			TEXT("Line_NoStrength,Line,Bad,10,0,0,0,0,0,4000,0,None,0,qa\n")
			TEXT("Line_NegStrength,Line,Bad,10,0,0,0,0,-10,4000,0,None,0,qa\n")
			TEXT("Line_NoSpool,Line,Bad,10,0,0,0,0,10,0,0,None,0,qa\n")
			TEXT("Line_NegSpool,Line,Bad,10,0,0,0,0,10,-4000,0,None,0,qa\n")
			TEXT("Hook_Ok,Hook,Ok,10,0,0,0,0,0,0,1.0,Bait.Shrimp,0,control\n")
			TEXT("Hook_NoSecurity,Hook,Bad,10,0,0,0,0,0,0,0,Bait.Shrimp,0,qa\n")
			TEXT("Hook_NegSecurity,Hook,Bad,10,0,0,0,0,0,0,-1,Bait.Shrimp,0,qa\n")
			TEXT("Hook_NegLuck,Hook,Bad,10,0,0,0,0,0,0,1.0,Bait.Shrimp,-0.5,qa\n"));
		TStrongObjectPtr<UDataTable> Gear;
		if (!MakeTableChecked(*this, Gear, FLureGearRow::StaticStruct(), Csv, false, TEXT("fixture DT_Gear")))
		{
			return false;
		}
		for (const TPair<FName, uint8*>& Pair : Gear->GetRowMap())
		{
			const FLureGearRow& Row = *reinterpret_cast<const FLureGearRow*>(Pair.Value);
			const FString Name = Pair.Key.ToString();
			const bool bControl = Name.EndsWith(TEXT("_Ok"));
			FString Problem;
			TestEqual(FString::Printf(TEXT("%s: Validate (%s)"), *Name, *Problem), Row.Validate(Problem), bControl);
			TestEqual(Name + TEXT(": CanEquip in its slot"), FLureGear::CanEquip(Gear.Get(), Row.Slot, Pair.Key), bControl);
			FLureGearLoadout Loadout;
			Loadout.Set(Row.Slot, Pair.Key);
			TArray<FString> Problems;
			const FLureGearStats Stats = FLureGear::Resolve(Gear.Get(), Loadout, &Problems);
			FLureGearStats BuiltIn;
			FLureGear::ApplyItem(BuiltIn, FLureGear::GetFallbackItem(Row.Slot), NAME_None);
			const FName UsedId = Row.Slot == ELureGearSlot::Rod ? Stats.RodId : Row.Slot == ELureGearSlot::Line ? Stats.LineId : Stats.HookId;
			if (bControl)
			{
				TestEqual(Name + TEXT(": the control row is used"), UsedId, Pair.Key);
			}
			else
			{
				TestTrue(Name + TEXT(": the slot falls back to the built-in item (id None)"), UsedId.IsNone() && Stats.bUsedFallback);
				const bool bSameAsBuiltIn = Row.Slot == ELureGearSlot::Rod ? (Stats.RodPower == BuiltIn.RodPower && Stats.ReelSpeed == BuiltIn.ReelSpeed && Stats.Drag == BuiltIn.Drag)
					: Row.Slot == ELureGearSlot::Line ? (Stats.LineStrength == BuiltIn.LineStrength && Stats.SpoolLength == BuiltIn.SpoolLength)
					: (Stats.HookSecurity == BuiltIn.HookSecurity && Stats.Luck == BuiltIn.Luck);
				TestTrue(Name + TEXT(": ... with the built-in numbers, never the bad ones"), bSameAsBuiltIn);
				TestTrue(Name + TEXT(": ... and the problem is reported"), Problems.ContainsByPredicate([&Name](const FString& Line) { return Line.Contains(Name); }));
			}
		}

		// Non-finite values can't be typed into the CSV but can reach a row (a struct built in code): refused too.
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		const float Inf = std::numeric_limits<float>::infinity();
		struct FGearBreaker { const TCHAR* Name; TFunction<void(FLureGearRow&)> Apply; };
		const FGearBreaker Breakers[] = {
			{ TEXT("rod power NaN"), [NaN](FLureGearRow& R) { R.Slot = ELureGearSlot::Rod; R.RodPower = NaN; } },
			{ TEXT("rod drag +Inf"), [Inf](FLureGearRow& R) { R.Slot = ELureGearSlot::Rod; R.Drag = Inf; } },
			{ TEXT("reel speed +Inf"), [Inf](FLureGearRow& R) { R.Slot = ELureGearSlot::Rod; R.ReelSpeed = Inf; } },
			{ TEXT("line strength +Inf"), [Inf](FLureGearRow& R) { R.Slot = ELureGearSlot::Line; R.LineStrength = Inf; } },
			{ TEXT("spool NaN"), [NaN](FLureGearRow& R) { R.Slot = ELureGearSlot::Line; R.SpoolLength = NaN; } },
			{ TEXT("hook security NaN"), [NaN](FLureGearRow& R) { R.Slot = ELureGearSlot::Hook; R.HookSecurity = NaN; } },
			{ TEXT("luck +Inf"), [Inf](FLureGearRow& R) { R.Slot = ELureGearSlot::Hook; R.Luck = Inf; } },
		};
		for (const FGearBreaker& Breaker : Breakers)
		{
			FLureGearRow Row;
			Breaker.Apply(Row);
			FString Problem;
			TestFalse(FString::Printf(TEXT("%s: invalid"), Breaker.Name), Row.Validate(Problem));
		}

		// An unknown slot name: the row must never become a usable item.
		const FString BadSlot = DataQA::GearCsv(TEXT("Reel_Fast,Reel,Bad,10,8,120,5,1.0,10,4000,1.0,Bait.Shrimp,0,qa\n"));
		TStrongObjectPtr<UDataTable> BadSlotTable;
		const TArray<FString> BadSlotProblems = MakeTable(BadSlotTable, FLureGearRow::StaticStruct(), BadSlot, false);
		bool bEquippable = false;
		for (const ELureGearSlot Slot : { ELureGearSlot::Rod, ELureGearSlot::Line, ELureGearSlot::Hook })
		{
			bEquippable |= FLureGear::CanEquip(BadSlotTable.Get(), Slot, TEXT("Reel_Fast"));
		}
		TestTrue(FString::Printf(TEXT("an unknown Slot 'Reel' is an import problem or an unusable row (import problems: %d)"), BadSlotProblems.Num()), BadSlotProblems.Num() > 0 || !bEquippable);
		return true;
	}

	// =================================================================================================================
	// DT_FightPattern
	// =================================================================================================================

	/** Every shipped DT_FightPattern row, against the rules restated from the spec and the FLureFightMove contract. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQADataPatternEveryRow, "Project.Fishing.Fight.QA.Data.PatternEveryRow", Flags)
	bool FLureFightQADataPatternEveryRow::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		const TArray<FString> Names = DataQA::JsonRowNames(*this, Data.PatternJson);
		DataQA::TestUniqueNames(*this, Names, TEXT("DT_FightPattern.json"));
		TestEqual(TEXT("every JSON row became a table row"), Data.Patterns->GetRowMap().Num(), Names.Num());
		for (const TPair<FName, uint8*>& Pair : Data.Patterns->GetRowMap())
		{
			const FLureFightPatternRow& Row = *reinterpret_cast<const FLureFightPatternRow*>(Pair.Value);
			const FString Name = Pair.Key.ToString();
			TestFalse(Name + TEXT(": has a display name"), Row.DisplayName.IsEmpty());
			if (!TestTrue(Name + TEXT(": has moves"), Row.Moves.Num() > 0))
			{
				continue;
			}
			TSet<FString> Ids;
			bool bPlainWeight = false;
			bool bFights = false;
			for (const FLureFightMove& Move : Row.Moves)
			{
				const FString Where = Name + TEXT(".") + Move.Id.ToString();
				TestFalse(Where + TEXT(": the move has an id"), Move.Id.IsNone());
				TestFalse(Where + TEXT(": the id is unique in the pattern"), Ids.Contains(Move.Id.ToString().ToLower()));
				Ids.Add(Move.Id.ToString().ToLower());
				TestFalse(Where + TEXT(": has a HUD label"), Move.Label.IsEmpty());
				TestTrue(Where + TEXT(": weights, durations, pull and speed finite and >= 0"),
					DataQA::FiniteNonNegative({ Move.Weight, Move.AggressionWeight, Move.DurationMin, Move.DurationMax, Move.Pull, Move.Speed }));
				TestTrue(FString::Printf(TEXT("%s: 0 < DurationMin <= DurationMax (%.2f, %.2f)"), *Where, Move.DurationMin, Move.DurationMax), Move.DurationMin > 0.f && Move.DurationMin <= Move.DurationMax);
				TestTrue(Where + TEXT(": Away, Side and Down in [-1, 1]"), FMath::Abs(Move.Away) <= 1.f && FMath::Abs(Move.Side) <= 1.f && FMath::Abs(Move.Down) <= 1.f);
				TestTrue(FString::Printf(TEXT("%s: a move lasts less than a minute (%.1f s)"), *Where, Move.DurationMax), Move.DurationMax <= 60.f);
				bPlainWeight |= Move.Weight > 0.f;
				bFights |= !Move.Rest && Move.Pull > 0.f;
			}
			TestTrue(Name + TEXT(": some move has a plain Weight > 0 (a calm fish, aggression 0, can pick it)"), bPlainWeight);
			TestTrue(Name + TEXT(": some non-rest move pulls (the fish fights)"), bFights);
			TestTrue(Name + TEXT(": OpeningMove is None or one of its moves"), Row.OpeningMove.IsNone() || Row.FindMove(Row.OpeningMove) != INDEX_NONE);
			FString Problem;
			TestTrue(Name + TEXT(": FLureFightPatternRow::Validate agrees: ") + Problem, Row.Validate(Problem));
		}
		// Every species fights with a real, valid pattern row (an unknown id would silently use the built-in pattern).
		for (const TPair<FName, uint8*>& Pair : Fish.Species->GetRowMap())
		{
			const FFishSpeciesRow& Species = *reinterpret_cast<const FFishSpeciesRow*>(Pair.Value);
			const FLureFightPatternRow* Pattern = Data.Pattern(Species.FightPatternId);
			FString Problem;
			TestTrue(FString::Printf(TEXT("species %s: FightPatternId '%s' is a valid DT_FightPattern row"), *Pair.Key.ToString(), *Species.FightPatternId.ToString()),
				!Species.FightPatternId.IsNone() && Pattern && Pattern->Validate(Problem));
		}
		FString Problem;
		TestTrue(TEXT("the built-in pattern (missing/invalid ids) is valid: ") + Problem, FLureFightPatternRow::GetFallbackPattern().Validate(Problem));
		return true;
	}

	/** Bad DT_FightPattern rows typed into the JSON (negative values, bad ranges, broken ids) are refused. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQADataPatternBadRows, "Project.Fishing.Fight.QA.Data.PatternBadRowsRejected", Flags)
	bool FLureFightQADataPatternBadRows::RunTest(const FString& Parameters)
	{
		using DataQA::MoveJson;
		struct FCase { const TCHAR* Name; FString Moves; const TCHAR* Opening; bool bValid; };
		const FCase Cases[] = {
			{ TEXT("Control"), MoveJson(TEXT("A")) + TEXT(", ") + MoveJson(TEXT("B")), TEXT("A"), true },
			{ TEXT("ControlNoOpening"), MoveJson(TEXT("A")), TEXT("None"), true },
			{ TEXT("NoMoves"), TEXT(""), TEXT("None"), false },
			{ TEXT("NegativeWeight"), MoveJson(TEXT("A"), { { TEXT("Weight"), TEXT("-1") } }) + TEXT(", ") + MoveJson(TEXT("B")), TEXT("B"), false },
			{ TEXT("NegativeAggressionWeight"), MoveJson(TEXT("A"), { { TEXT("AggressionWeight"), TEXT("-0.5") } }), TEXT("A"), false },
			{ TEXT("NegativePull"), MoveJson(TEXT("A"), { { TEXT("Pull"), TEXT("-1") } }), TEXT("A"), false },
			{ TEXT("NegativeSpeed"), MoveJson(TEXT("A"), { { TEXT("Speed"), TEXT("-2") } }), TEXT("A"), false },
			{ TEXT("ZeroDuration"), MoveJson(TEXT("A"), { { TEXT("DurationMin"), TEXT("0") } }), TEXT("A"), false },
			{ TEXT("NegativeDuration"), MoveJson(TEXT("A"), { { TEXT("DurationMin"), TEXT("-1") }, { TEXT("DurationMax"), TEXT("-0.5") } }), TEXT("A"), false },
			{ TEXT("MaxBelowMin"), MoveJson(TEXT("A"), { { TEXT("DurationMin"), TEXT("2") }, { TEXT("DurationMax"), TEXT("1") } }), TEXT("A"), false },
			{ TEXT("AwayAboveOne"), MoveJson(TEXT("A"), { { TEXT("Away"), TEXT("1.5") } }), TEXT("A"), false },
			{ TEXT("SideBelowMinusOne"), MoveJson(TEXT("A"), { { TEXT("Side"), TEXT("-1.5") } }), TEXT("A"), false },
			{ TEXT("DownAboveOne"), MoveJson(TEXT("A"), { { TEXT("Down"), TEXT("2") } }), TEXT("A"), false },
			{ TEXT("DuplicateIds"), MoveJson(TEXT("A")) + TEXT(", ") + MoveJson(TEXT("A")), TEXT("A"), false },
			{ TEXT("DuplicateIdsOtherCase"), MoveJson(TEXT("Run")) + TEXT(", ") + MoveJson(TEXT("run")), TEXT("Run"), false },
			{ TEXT("EmptyId"), MoveJson(TEXT("None")), TEXT("None"), false },
			{ TEXT("UnknownOpening"), MoveJson(TEXT("A")), TEXT("Nope"), false },
			{ TEXT("NoWeightAtAll"), MoveJson(TEXT("A"), { { TEXT("Weight"), TEXT("0") } }), TEXT("A"), false },
		};
		FString Json = TEXT("[");
		for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Cases)); ++Index)
		{
			Json += FString::Printf(TEXT("%s{ \"Name\": \"%s\", \"DisplayName\": \"%s\", \"OpeningMove\": \"%s\", \"Moves\": [ %s ] }"), Index > 0 ? TEXT(",\n") : TEXT(""),
				Cases[Index].Name, Cases[Index].Name, Cases[Index].Opening, *Cases[Index].Moves);
		}
		Json += TEXT("]");
		TStrongObjectPtr<UDataTable> Patterns;
		const TArray<FString> ImportProblems = MakeTable(Patterns, FLureFightPatternRow::StaticStruct(), Json, true);
		AddInfo(FString::Printf(TEXT("fixture import problems (informational): %d"), ImportProblems.Num()));
		for (const FCase& Case : Cases)
		{
			const FLureFightPatternRow* Row = Patterns->FindRow<FLureFightPatternRow>(Case.Name, TEXT("FightQA"), false);
			if (!TestNotNull(FString::Printf(TEXT("%s imported"), Case.Name), Row))
			{
				continue;
			}
			FString Problem;
			TestEqual(FString::Printf(TEXT("%s: Validate (%s)"), Case.Name, *Problem), Row->Validate(Problem), Case.bValid);
		}
		// Values JSON can't express (a row built in code) are refused too.
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		const float Inf = std::numeric_limits<float>::infinity();
		struct FMoveBreaker { const TCHAR* Name; TFunction<void(FLureFightMove&)> Apply; };
		const FMoveBreaker Breakers[] = {
			{ TEXT("pull NaN"), [NaN](FLureFightMove& M) { M.Pull = NaN; } },
			{ TEXT("speed +Inf"), [Inf](FLureFightMove& M) { M.Speed = Inf; } },
			{ TEXT("weight +Inf"), [Inf](FLureFightMove& M) { M.Weight = Inf; } },
			{ TEXT("duration max +Inf"), [Inf](FLureFightMove& M) { M.DurationMax = Inf; } },
			{ TEXT("away NaN"), [NaN](FLureFightMove& M) { M.Away = NaN; } },
		};
		for (const FMoveBreaker& Breaker : Breakers)
		{
			FLureFightMove Move = MakeMove(TEXT("A"), 1.f, 1.f, 0.5f, 1.f);
			Breaker.Apply(Move);
			const FLureFightPatternRow Pattern = MakePattern({ Move }, TEXT("A"));
			FString Problem;
			TestFalse(FString::Printf(TEXT("%s: invalid"), Breaker.Name), Pattern.Validate(Problem));
		}
		return true;
	}

	/**
	 *  "Validate: at least one move with a positive weight." A pattern whose moves only have AggressionWeight can't give a calm
	 *  fish (aggression 0, the DT_FishStat default) any move after the opening: it sulks with the tired pull and the HUD shows
	 *  the move "None". The validator must refuse it (so the fight falls back to the built-in pattern with a warning).
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQADataPatternPlainWeight, "Project.Fishing.Fight.QA.Data.PatternNeedsAPlainWeight", Flags)
	bool FLureFightQADataPatternPlainWeight::RunTest(const FString& Parameters)
	{
		const FLureFightPatternRow AggressiveOnly = MakePattern({
			MakeMove(TEXT("Charge"), 1.5f, 1.f, 1.f, 1.f, false, 0.f, 0.2f),
			MakeMove(TEXT("Thrash"), 1.2f, 0.5f, 0.2f, 1.f, false, 0.f, 0.1f) }, TEXT("Charge"));
		// The consequence, from the documented pick rule: no move can be picked for aggression 0.
		TestEqual(TEXT("fixture: a calm fish can pick no move from it"), FLureFight::PickMove(AggressiveOnly, 0.f, 0.5f), static_cast<int32>(INDEX_NONE));
		FLureFightState S;
		FLureFight::Begin(S, MakeFightFish(4.f, 100.f, 1.0e9f, 0.f), AggressiveOnly, TEXT("QA"), MakeGear(8.f, 120.f, 5.f, 100.f, 100000.f, 100.f),
			FLureFishFightRow::GetFallbackRow(), 1, 1000.f);
		for (int32 Step = 0; Step < 5 * 60; ++Step)
		{
			FLureFight::Step(S, Input(false));
		}
		AddInfo(FString::Printf(TEXT("a calm fish on it after 5 s: move '%s', exhausted %d, pull %.2f (the tired pull %.2f)"), *S.GetMoveId().ToString(), S.bExhausted,
			S.Pull, 4.f * FLureFishFightRow::GetFallbackRow().TiredPull));
		FString Problem;
		TestFalse(FString::Printf(TEXT("a pattern with no plain Weight > 0 is invalid (Validate said: %s)"), Problem.IsEmpty() ? TEXT("valid") : *Problem), AggressiveOnly.Validate(Problem));
		return true;
	}

	// =================================================================================================================
	// DT_FishFight
	// =================================================================================================================

	/** Every DT_FishFight row, against the rules restated from the spec, plus the design invariants the rules rely on. */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQADataFishFightEveryRow, "Project.Fishing.Fight.QA.Data.FishFightEveryRow", Flags)
	bool FLureFightQADataFishFightEveryRow::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		DataQA::TestUniqueNames(*this, DataQA::CsvRowNames(Data.FightCsv), TEXT("DT_FishFight.csv"));
		TestNotNull(TEXT("the settings' FishFightRow exists"), Data.Tuning());
		const FGameplayTag StatRoot = Tag(TEXT("Fish.Stat"));
		for (const TPair<FName, uint8*>& Pair : Data.Fight->GetRowMap())
		{
			const FLureFishFightRow& R = *reinterpret_cast<const FLureFishFightRow*>(Pair.Value);
			const FString Name = Pair.Key.ToString();
			TestTrue(Name + TEXT(": every number finite and >= 0"), DataQA::FiniteNonNegative({ R.PullPerStrength, R.SpeedPerStat, R.StaminaPerStat, R.RestDifficultyExponent,
				R.TiredPull, R.ExhaustedStamina, R.StaminaRecovery, R.ReelStrain, R.ReelLoad, R.DragHold, R.TensionRiseTime, R.TensionFallTime, R.SnapGraceTime, R.SlackShare,
				R.SlackGraceTime, R.LandDistance, R.MaxDepth, R.DepthRecovery, R.MaxSideDeg, R.DiveBobberShare, R.RodTensionPitchDeg, R.RodShakeDeg, R.TautTension }));
			TSet<FGameplayTag> Stats;
			for (const FGameplayTag& Stat : { R.StrengthStat, R.StaminaStat, R.SpeedStat, R.AggressionStat })
			{
				TestTrue(FString::Printf(TEXT("%s: stat '%s' is a registered Fish.Stat.* tag"), *Name, *Stat.ToString()), Stat.IsValid() && Stat.MatchesTag(StatRoot) && Stat != StatRoot);
				TestNotNull(FString::Printf(TEXT("%s: stat '%s' has a DT_FishStat row"), *Name, *Stat.ToString()), Fish.Get().FindStat(Stat));
				Stats.Add(Stat);
			}
			TestEqual(Name + TEXT(": four different stats drive the fight"), Stats.Num(), 4);
			TestTrue(Name + TEXT(": PullPerStrength, SpeedPerStat and StaminaPerStat > 0"), R.PullPerStrength > 0.f && R.SpeedPerStat > 0.f && R.StaminaPerStat > 0.f);
			TestTrue(Name + TEXT(": TiredPull and DiveBobberShare in [0, 1], ExhaustedStamina and DragHold in [0, 1)"), R.TiredPull <= 1.f && R.DiveBobberShare <= 1.f && R.ExhaustedStamina < 1.f && R.DragHold < 1.f);
			TestTrue(Name + TEXT(": SlackGraceTime, LandDistance and TautTension > 0, TautTension <= 1"), R.SlackGraceTime > 0.f && R.LandDistance > 0.f && R.TautTension > 0.f && R.TautTension <= 1.f);
			TestTrue(FString::Printf(TEXT("%s: SimRate %d in [10, 240]"), *Name, R.SimRate), R.SimRate >= 10 && R.SimRate <= 240);
			TestTrue(FString::Printf(TEXT("%s: MaxSideDeg %.0f in [0, 170]"), *Name, R.MaxSideDeg), R.MaxSideDeg <= 170.f);
			// The rules rely on these relations (spec, "The step"):
			TestTrue(Name + TEXT(": a tired fish on a loose line counts as slack (TiredPull < SlackShare), so never reeling loses it"), R.TiredPull < R.SlackShare);
			TestTrue(FString::Printf(TEXT("%s: reeling a tired fish keeps the line taut for any fish size (TiredPull x ReelStrain %.3f >= SlackShare %.3f)"), *Name, R.TiredPull * R.ReelStrain, R.SlackShare),
				R.TiredPull * R.ReelStrain >= R.SlackShare);
			TestTrue(Name + TEXT(": cranking loads the line more than the pull alone (ReelStrain >= 1)"), R.ReelStrain >= 1.f);
			FString Problem;
			TestTrue(Name + TEXT(": FLureFishFightRow::Validate agrees: ") + Problem, R.Validate(Problem));
		}
		return true;
	}

	/** Bad DT_FishFight rows typed into the CSV are refused, and the component then fights with the built-in tuning (one warning). */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQADataFishFightBadRows, "Project.Fishing.Fight.QA.Data.FishFightBadRowsRejected", Flags)
	bool FLureFightQADataFishFightBadRows::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		TArray<FString> Lines;
		Data.FightCsv.ParseIntoArrayLines(Lines, true);
		if (!TestTrue(TEXT("DT_FishFight.csv has a header and a row"), Lines.Num() >= 2))
		{
			return false;
		}
		TArray<FString> Header;
		Lines[0].ParseIntoArray(Header, TEXT(","), false);
		TArray<FString> Shipped;
		Lines[1].ParseIntoArray(Shipped, TEXT(","), false);
		// One bad row per case: the shipped row with one cell changed.
		struct FCase { const TCHAR* Column; const TCHAR* Value; };
		const FCase Cases[] = {
			{ TEXT("PullPerStrength"), TEXT("-0.25") }, { TEXT("PullPerStrength"), TEXT("0") }, { TEXT("StaminaPerStat"), TEXT("0") }, { TEXT("SpeedPerStat"), TEXT("-5") },
			{ TEXT("TiredPull"), TEXT("1.5") }, { TEXT("ExhaustedStamina"), TEXT("1") }, { TEXT("DragHold"), TEXT("1") }, { TEXT("DragHold"), TEXT("-0.5") },
			{ TEXT("SlackGraceTime"), TEXT("0") }, { TEXT("LandDistance"), TEXT("0") }, { TEXT("TautTension"), TEXT("0") }, { TEXT("SimRate"), TEXT("9") },
			{ TEXT("SimRate"), TEXT("241") }, { TEXT("SnapGraceTime"), TEXT("-0.6") }, { TEXT("TensionRiseTime"), TEXT("-0.1") }, { TEXT("ReelStrain"), TEXT("-1.3") },
			{ TEXT("StaminaRecovery"), TEXT("-0.04") }, { TEXT("SlackShare"), TEXT("-0.35") }, { TEXT("StrengthStat"), TEXT("None") }, { TEXT("AggressionStat"), TEXT("None") },
		};
		FString Csv = Lines[0] + TEXT("\n");
		int32 CaseIndex = 0;
		for (const FCase& Case : Cases)
		{
			const int32 Column = Header.IndexOfByPredicate([&Case](const FString& H) { return H.TrimStartAndEnd() == Case.Column; });
			if (!TestTrue(FString::Printf(TEXT("column %s exists"), Case.Column), Column != INDEX_NONE && Shipped.IsValidIndex(Column)))
			{
				continue;
			}
			TArray<FString> Row = Shipped;
			Row[0] = FString::Printf(TEXT("Bad%02d"), CaseIndex++);
			Row[Column] = Case.Value;
			Csv += FString::Join(Row, TEXT(",")) + TEXT("\n");
		}
		TStrongObjectPtr<UDataTable> Table;
		if (!MakeTableChecked(*this, Table, FLureFishFightRow::StaticStruct(), Csv, false, TEXT("fixture DT_FishFight")))
		{
			return false;
		}
		CaseIndex = 0;
		for (const FCase& Case : Cases)
		{
			const FLureFishFightRow* Row = Table->FindRow<FLureFishFightRow>(FName(*FString::Printf(TEXT("Bad%02d"), CaseIndex++)), TEXT("FightQA"), false);
			FString Problem;
			if (TestNotNull(FString::Printf(TEXT("%s = %s imported"), Case.Column, Case.Value), Row))
			{
				TestFalse(FString::Printf(TEXT("%s = %s is invalid"), Case.Column, Case.Value), Row->Validate(Problem));
			}
		}
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		for (float FLureFishFightRow::* Field : { &FLureFishFightRow::ReelLoad, &FLureFishFightRow::SnapGraceTime, &FLureFishFightRow::TensionFallTime, &FLureFishFightRow::MaxDepth })
		{
			FLureFishFightRow Row = *Data.Tuning();
			Row.*Field = NaN;
			FString Problem;
			TestFalse(TEXT("a NaN value is invalid"), Row.Validate(Problem));
		}

		// The component with an invalid settings row: the built-in tuning, one warning naming the table.
		FString BadDefault = Lines[0] + TEXT("\n");
		{
			TArray<FString> Row = Shipped;
			Row[0] = GetDefault<ULureFishingSettings>()->FishFightRow.ToString();
			const int32 Column = Header.IndexOfByPredicate([](const FString& H) { return H.TrimStartAndEnd() == TEXT("SimRate"); });
			Row[Column] = TEXT("0");
			BadDefault += FString::Join(Row, TEXT(",")) + TEXT("\n");
		}
		TStrongObjectPtr<UDataTable> BadTable;
		if (!MakeTableChecked(*this, BadTable, FLureFishFightRow::StaticStruct(), BadDefault, false, TEXT("fixture DT_FishFight (bad Default)")))
		{
			return false;
		}
		TStrongObjectPtr<ULureFishingComponent> Component(NewObject<ULureFishingComponent>(GetTransientPackage()));
		AddExpectedMessage(TEXT("using the built-in fight tuning"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		Component->SetFightTables(Data.Gear.Get(), Data.Patterns.Get(), BadTable.Get());
		const FLureFishFightRow& Used = Component->GetFightTuning();
		const FLureFishFightRow BuiltIn = FLureFishFightRow::GetFallbackRow();
		TestTrue(TEXT("an invalid settings row: the component uses the built-in tuning"), FLureFishFightRow::StaticStruct()->CompareScriptStruct(&Used, &BuiltIn, PPF_None));
		return true;
	}

	/**
	 *  DT_FishFight's four stat columns must name fish stats ("must be registered Fish.Stat tags", FLureFishFightRow::Validate):
	 *  a registered tag from another family imports cleanly, so only the validator can stop it. With StrengthStat = Bait.Shrimp
	 *  every fish reads a strength of 0 and pulls the minimum: the fight becomes trivial.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQADataFishFightStatFamily, "Project.Fishing.Fight.QA.Data.FishFightStatTagsMustBeFishStats", Flags)
	bool FLureFightQADataFishFightStatFamily::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		FishQA::FTables Fish;
		if (!Data.Load(*this) || !FishQA::LoadReal(*this, Fish))
		{
			return false;
		}
		FFishInstance Snapper;
		if (!RollFish(*this, Fish, TEXT("CoralSnapper"), TEXT("Common"), 0.5f, 5, Snapper))
		{
			return false;
		}
		const FFishLevelScaling& Scaling = GetDefault<UFishSettings>()->LevelScaling;
		for (const TCHAR* Wrong : { TEXT("Bait.Shrimp"), TEXT("Habitat.Reef") })
		{
			if (!TestTrue(FString::Printf(TEXT("fixture: %s is a registered tag"), Wrong), Tag(Wrong).IsValid()))
			{
				continue;
			}
			FLureFishFightRow Row = *Data.Tuning();
			Row.StrengthStat = Tag(Wrong);
			AddInfo(FString::Printf(TEXT("StrengthStat = %s: the snapper's base pull is %.3f instead of %.3f"), Wrong,
				FLureFight::MakeFish(Snapper, Row, 1, Scaling).BasePull, FLureFight::MakeFish(Snapper, *Data.Tuning(), 1, Scaling).BasePull));
			FString Problem;
			TestFalse(FString::Printf(TEXT("StrengthStat = %s (not a Fish.Stat tag) is invalid (Validate said: %s)"), Wrong, Problem.IsEmpty() ? TEXT("valid") : *Problem), Row.Validate(Problem));
		}
		return true;
	}

	// =================================================================================================================
	// Cross-table: DT_Gear x DT_Fishing x DT_FishFight
	// =================================================================================================================

	/**
	 *  Distances that must fit together, or a fight ends on its first step or never starts:
	 *  - the shortest cast of every rod lands beyond LandDistance (else a short cast is "landed" the moment it's hooked);
	 *  - the longest cast of every rod stays within MaxLineLength (else the line comes in on its own right after the cast);
	 *  - every line's spool holds more than MaxLineLength (a hook can happen that far out; a shorter spool is "spooled" at once).
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLureFightQADataCrossTable, "Project.Fishing.Fight.QA.Data.CrossTableDistances", Flags)
	bool FLureFightQADataCrossTable::RunTest(const FString& Parameters)
	{
		FFightTables Data;
		if (!Data.Load(*this))
		{
			return false;
		}
		FString FishingCsv;
		TStrongObjectPtr<UDataTable> FishingTable;
		if (!ReadSource(*this, TEXT("DT_Fishing.csv"), FishingCsv) || !MakeTableChecked(*this, FishingTable, FLureFishingRow::StaticStruct(), FishingCsv, false, TEXT("DT_Fishing.csv")))
		{
			return false;
		}
		const FLureFishFightRow& Tuning = *Data.Tuning();
		for (const TPair<FName, uint8*>& FishingPair : FishingTable->GetRowMap())
		{
			const FLureFishingRow& Fishing = *reinterpret_cast<const FLureFishingRow*>(FishingPair.Value);
			const FString Profile = FishingPair.Key.ToString();
			TestEqual(Profile + TEXT(": the shipped profile runs the reel fight (AutoLandDelay 0)"), Fishing.AutoLandDelay, 0.f);
			for (const TPair<FName, uint8*>& GearPair : Data.Gear->GetRowMap())
			{
				const FLureGearRow& Item = *reinterpret_cast<const FLureGearRow*>(GearPair.Value);
				const FString Name = GearPair.Key.ToString();
				if (Item.Slot == ELureGearSlot::Rod)
				{
					const float Shortest = Fishing.MinCastDistance * Item.CastDistanceMultiplier;
					const float Longest = Fishing.MaxCastDistance * Item.CastDistanceMultiplier;
					TestTrue(FString::Printf(TEXT("%s x %s: the shortest cast (%.0f cm) lands beyond LandDistance (%.0f cm)"), *Profile, *Name, Shortest, Tuning.LandDistance), Shortest > Tuning.LandDistance);
					TestTrue(FString::Printf(TEXT("%s x %s: the longest cast (%.0f cm) is within MaxLineLength (%.0f cm)"), *Profile, *Name, Longest, Fishing.MaxLineLength), Longest <= Fishing.MaxLineLength);
				}
				else if (Item.Slot == ELureGearSlot::Line)
				{
					TestTrue(FString::Printf(TEXT("%s x %s: the spool (%.0f cm) holds more than MaxLineLength (%.0f cm)"), *Profile, *Name, Item.SpoolLength, Fishing.MaxLineLength), Item.SpoolLength > Fishing.MaxLineLength);
				}
			}
		}
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

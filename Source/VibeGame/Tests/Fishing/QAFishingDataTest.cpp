// Lure T-006 QA (qa-engineer): data validation for DT_Fishing and the new DT_Movement rod columns, and the fishing input actions.
// Project.Fishing.QA.Data.* and Project.Fishing.QA.Input.* - every row, every cell (the CSV importer turns a typo in a number cell
// into 0 without reporting it, see T004-Q2), the design limits from fishing-rules.md / ART_STYLE.md / designer B-S3/B-S4, and the
// runtime Validate() gates.

#include "Tests/Fishing/QAFishingTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LureInputSubsystem.h"
#include "Character/LurePlayerCharacter.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "Engine/DataTable.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"
#include <limits>

// Everything lives in namespace QAFishing (unity builds merge test files; other files use global using-directives).
namespace QAFishing
{

namespace DataLocal
{

	bool HasDigit(const FString& Text)
	{
		for (const TCHAR Char : Text)
		{
			if (FChar::IsDigit(Char))
			{
				return true;
			}
		}
		return false;
	}

	/** The authored names of an enum, without the generated _MAX entry. */
	TArray<FString> EnumNames(const UEnum* Enum)
	{
		TArray<FString> Names;
		const int32 Count = Enum->NumEnums() - (Enum->ContainsExistingMax() ? 1 : 0);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Names.Add(Enum->GetNameStringByIndex(Index));
		}
		return Names;
	}

	/**
	 *  Every cell of Csv is well-typed for Struct AND the importer understood it the same way: numbers are numbers (and import as that
	 *  number), ints have no fraction, bools are True/False, enums are exactly one of the authored names (never _MAX). The header has
	 *  every struct field and no unknown column; row names are unique (case-insensitive, like FName).
	 */
	void CheckTypedCsv(FAutomationTestBase& Test, const FString& Csv, UScriptStruct* Struct, const TCHAR* What)
	{
		TArray<FString> Header;
		TArray<TArray<FString>> Rows;
		SplitCsv(Csv, Header, Rows);
		if (!Test.TestTrue(FString::Printf(TEXT("%s: a header and rows"), What), Header.Num() > 1 && Rows.Num() > 0))
		{
			return;
		}
		TArray<FString> Missing;
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (!Header.Contains(It->GetName()))
			{
				Missing.Add(It->GetName());
			}
		}
		Test.TestEqual(FString::Printf(TEXT("%s: every field is a column (missing: %s)"), What, *FString::Join(Missing, TEXT(", "))), Missing.Num(), 0);
		TArray<FString> Problems;
		const TStrongObjectPtr<UDataTable> Table(MakeTable(Struct, Csv, &Problems));
		Test.TestEqual(FString::Printf(TEXT("%s: imports with no problems (%s)"), What, *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
		TSet<FString> Seen;
		for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
		{
			const TArray<FString>& Row = Rows[RowIndex];
			const FString RowName = Row.Num() > 0 ? Row[0] : FString();
			bool bDuplicate = false;
			Seen.Add(RowName.ToLower(), &bDuplicate);
			Test.TestFalse(FString::Printf(TEXT("%s: row '%s' appears once"), What, *RowName), bDuplicate);
			Test.TestEqual(FString::Printf(TEXT("%s: row '%s' has one cell per column"), What, *RowName), Row.Num(), Header.Num());
			const uint8* Data = Table->FindRowUnchecked(FName(*RowName));
			if (!Test.TestNotNull(FString::Printf(TEXT("%s: row '%s' imported"), What, *RowName), Data))
			{
				continue;
			}
			for (int32 Column = 1; Column < FMath::Min(Row.Num(), Header.Num()); ++Column)
			{
				const FString& Cell = Row[Column];
				const FString Where = FString::Printf(TEXT("%s %s.%s = '%s'"), What, *RowName, *Header[Column], *Cell);
				const FProperty* Property = Struct->FindPropertyByName(FName(*Header[Column]));
				if (!Test.TestNotNull(Where + TEXT(": a known column"), Property))
				{
					continue;
				}
				if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
				{
					const bool bTrue = Cell.Equals(TEXT("True"), ESearchCase::IgnoreCase);
					if (Test.TestTrue(Where + TEXT(" is True or False"), bTrue || Cell.Equals(TEXT("False"), ESearchCase::IgnoreCase)))
					{
						Test.TestEqual(Where + TEXT(" imports as written"), Bool->GetPropertyValue_InContainer(Data), bTrue);
					}
				}
				else if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
				{
					const TArray<FString> Names = EnumNames(Enum->GetEnum());
					const int32 Index = Names.IndexOfByKey(Cell); // exact, case-sensitive
					if (Test.TestTrue(FString::Printf(TEXT("%s is one of %s (exact name, never _MAX)"), *Where, *FString::Join(Names, TEXT("/"))), Index != INDEX_NONE))
					{
						const int64 Imported = Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Enum->ContainerPtrToValuePtr<void>(Data));
						Test.TestEqual(Where + TEXT(" imports as written"), Imported, Enum->GetEnum()->GetValueByIndex(Index));
					}
				}
				else if (const FIntProperty* Int = CastField<FIntProperty>(Property))
				{
					if (Test.TestTrue(Where + TEXT(" is a whole number"), FCString::IsNumeric(*Cell) && HasDigit(Cell) && !Cell.Contains(TEXT("."))))
					{
						Test.TestEqual(Where + TEXT(" imports as written"), Int->GetPropertyValue_InContainer(Data), FCString::Atoi(*Cell));
					}
				}
				else if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
				{
					if (Test.TestTrue(Where + TEXT(" is a number"), FCString::IsNumeric(*Cell) && HasDigit(Cell)))
					{
						const double Expected = FCString::Atod(*Cell);
						Test.TestTrue(Where + TEXT(" imports as written"), FMath::IsNearlyEqual(static_cast<double>(Float->GetPropertyValue_InContainer(Data)), Expected, FMath::Max(1.0e-6, FMath::Abs(Expected) * 1.0e-6)));
					}
				}
				else
				{
					Test.AddError(Where + TEXT(": QA has no cell rule for type ") + Property->GetCPPType() + TEXT(" (add one)"));
				}
			}
		}
	}

	/** Sets a float or int field of Row (by name) through reflection. */
	bool SetField(FAutomationTestBase& Test, UScriptStruct* Struct, void* Row, const TCHAR* Name, double Value)
	{
		const FProperty* Property = Struct->FindPropertyByName(FName(Name));
		if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
		{
			Float->SetPropertyValue_InContainer(Row, static_cast<float>(Value));
			return true;
		}
		if (const FIntProperty* Int = CastField<FIntProperty>(Property))
		{
			Int->SetPropertyValue_InContainer(Row, static_cast<int32>(Value));
			return true;
		}
		Test.AddError(FString::Printf(TEXT("QA fixture: no number field %s"), Name));
		return false;
	}

	struct FMutation
	{
		const TCHAR* Field;
		double Value;
		const TCHAR* Field2 = nullptr;
		double Value2 = 0.0;
	};
}

using namespace DataLocal;

// =====================================================================================================================
// DT_Fishing
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishDataFishingCsvCellsAreTyped, "Project.Fishing.QA.Data.FishingCsvCellsAreTyped", QAFishing::Flags)
bool FQAFishDataFishingCsvCellsAreTyped::RunTest(const FString& Parameters)
{
	FString Csv;
	if (!LoadFishingCsv(*this, Csv))
	{
		return false;
	}
	CheckTypedCsv(*this, Csv, FLureFishingRow::StaticStruct(), TEXT("DT_Fishing"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishDataFishingRowsMeetDesignLimits, "Project.Fishing.QA.Data.FishingRowsMeetDesignLimits", QAFishing::Flags)
bool FQAFishDataFishingRowsMeetDesignLimits::RunTest(const FString& Parameters)
{
	FString Csv;
	if (!LoadFishingCsv(*this, Csv))
	{
		return false;
	}
	const TStrongObjectPtr<UDataTable> Table(MakeTableChecked(*this, FLureFishingRow::StaticStruct(), Csv, TEXT("DT_Fishing")));
	TestNotNull(TEXT("a Default row (the settings' DefaultProfileRow)"), Table->FindRow<FLureFishingRow>(GetDefault<ULureFishingSettings>()->DefaultProfileRow, TEXT("QA"), false));
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FLureFishingRow& Row = *reinterpret_cast<const FLureFishingRow*>(Pair.Value);
		const FString Name = Pair.Key.ToString();
		FString Problem;
		const bool bValid = Row.Validate(Problem);
		TestTrue(Name + TEXT(": passes the runtime Validate() ") + Problem, bValid);
		TestTrue(Name + TEXT(": min cast < max cast (charging must matter)"), Row.MinCastDistance < Row.MaxCastDistance);
		TestTrue(FString::Printf(TEXT("%s: MaxLineLength %.0f > MaxCastDistance %.0f (else a full cast comes straight back in)"), *Name, Row.MaxLineLength, Row.MaxCastDistance),
			Row.MaxLineLength > Row.MaxCastDistance);
		TestTrue(Name + TEXT(": B-S3, the line is at least 2 px wide at 1080p"), Row.LinePixelWidth >= 2.f);
		// Design limit (designer must-fix, Saved/AgentLogs/design/20260924-2230-sprint1-build.md: "scale the bobber 1.5-2x" of 4.5 = 6.75-9;
		// T-075a ships 7.9, ART_STYLE.md). Never smaller than the old 4.5, never above 2x of it.
		TestTrue(FString::Printf(TEXT("%s: ART_STYLE/designer, bobber readability scale in [4.5, 9] (%.2f)"), *Name, Row.BobberScale), Row.BobberScale >= 4.5f && Row.BobberScale <= 9.f);
		TestTrue(FString::Printf(TEXT("%s: T-075a, BiteDipMinShare is a share in [0, 1] (%.2f)"), *Name, Row.BiteDipMinShare), Row.BiteDipMinShare >= 0.f && Row.BiteDipMinShare <= 1.f);
		TestTrue(FString::Printf(TEXT("%s: T-075a, BiteDipAttack is not negative and shorter than the hook window (%.2f s)"), *Name, Row.BiteDipAttack), Row.BiteDipAttack >= 0.f && Row.BiteDipAttack < Row.HookWindow);
		TestTrue(FString::Printf(TEXT("%s: B-S4, the bite pulls the whole red top under (%.1f cm >= 4.9 cm x %.1f)"), *Name, Row.BiteDipDepth, Row.BobberScale),
			Row.BiteDipDepth >= 4.9f * Row.BobberScale);
		TestTrue(Name + TEXT(": the bite is felt (rumble > 0 for > 0 s)"), Row.BiteRumbleIntensity > 0.f && Row.BiteRumbleIntensity <= 1.f && Row.BiteRumbleDuration > 0.f);
		TestTrue(FString::Printf(TEXT("%s: a humanly hookable window (%.2f s in [0.3, 2])"), *Name, Row.HookWindow), Row.HookWindow >= 0.3f && Row.HookWindow <= 2.f);
		TestTrue(Name + TEXT(": the network grace is a small extra, not the window"), Row.HookLatencyGrace >= 0.f && Row.HookLatencyGrace < Row.HookWindow);
		TestTrue(Name + TEXT(": two nibbles are two tells (a nibble ends before the next is due)"), Row.NibbleDuration < Row.NibbleInterval);
		TestTrue(Name + TEXT(": nibbles and bites fit the ~30 s loop (waits <= 30 s)"), Row.BiteWaitMax <= 30.f && Row.RebiteWaitMax <= 30.f && Row.BiteWaitMin > 0.f);
		// Lead decision 4: AutoLandDelay > 0 lands a hooked fish on its own (placeholder); 0 = it stays hooked for the T-007 reel fight.
		// Read from the table, never hard-coded: T-007 sets it to 0.
		TestTrue(FString::Printf(TEXT("%s: AutoLandDelay %.2f s is 0 (reel fight) or a short auto-land (<= 10 s)"), *Name, Row.AutoLandDelay), Row.AutoLandDelay >= 0.f && Row.AutoLandDelay <= 10.f);
		TestTrue(Name + TEXT(": the no-bite hint comes after a while"), Row.NoBiteHintDelay > 0.f);
	}
	if (const FLureFishingRow* Default = Table->FindRow<FLureFishingRow>(TEXT("Default"), TEXT("QA"), false))
	{
		TestEqual(TEXT("spec default: EarlyHook = ReelIn (press to reel in and recast)"), StaticEnum<ELureEarlyHookRule>()->GetNameStringByValue(static_cast<int64>(Default->EarlyHook)), FString(TEXT("ReelIn")));
		TestFalse(TEXT("spec default: MissEndsCast = False (the bobber stays after a miss)"), Default->MissEndsCast);
		TestNearlyEqual(TEXT("spec: HookLatencyGrace 0.15 s"), Default->HookLatencyGrace, 0.15f, 1.0e-4f);
		TestNearlyEqual(TEXT("spec: NoBiteHintDelay 8 s"), Default->NoBiteHintDelay, 8.f, 1.0e-4f);
		TestTrue(FString::Printf(TEXT("designer 2026-09-24: Default BobberScale 1.5-2x of 4.5, i.e. [6.75, 9] (%.2f)"), Default->BobberScale), Default->BobberScale >= 6.75f && Default->BobberScale <= 9.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishDataFishingBuiltInProfileEqualsCsv, "Project.Fishing.QA.Data.FishingBuiltInProfileEqualsCsv", QAFishing::Flags)
bool FQAFishDataFishingBuiltInProfileEqualsCsv::RunTest(const FString& Parameters)
{
	// The fallback used when DT_Fishing is missing plays exactly like the shipped Default row (no surprise tuning in lanes/builds).
	FLureFishingRow Shipped;
	if (!ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	const FLureFishingRow BuiltIn = FLureFishingRules::GetFallbackRow();
	const TArray<FString> Differences = DifferentFields(FLureFishingRow::StaticStruct(), &BuiltIn, &Shipped);
	TestEqual(FString::Printf(TEXT("the built-in profile equals the CSV's Default row (differs in: %s)"), *FString::Join(Differences, TEXT(", "))), Differences.Num(), 0);
	FString Problem;
	const bool bValid = BuiltIn.Validate(Problem);
	TestTrue(TEXT("the built-in profile is valid ") + Problem, bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishDataFishingValidateRejectsBadRows, "Project.Fishing.QA.Data.FishingValidateRejectsBadRows", QAFishing::Flags)
bool FQAFishDataFishingValidateRejectsBadRows::RunTest(const FString& Parameters)
{
	// Contract: Validate() rejects rows that aren't usable (non-finite, negative, unordered ranges, zero times), and a table whose row
	// fails it is never used (the built-in profile is). Legal edge values stay valid.
	FLureFishingRow Shipped;
	if (!ShippedFishingRow(*this, Shipped))
	{
		return false;
	}
	UScriptStruct* Struct = FLureFishingRow::StaticStruct();
	const FMutation Bad[] = {
		{ TEXT("ChargeTime"), 0.0 }, { TEXT("ChargeTime"), -1.0 }, { TEXT("ChargeTime"), NaN }, { TEXT("ChargeExponent"), 0.0 },
		{ TEXT("MinCastDistance"), -5.0 }, { TEXT("MinCastDistance"), 900.0, TEXT("MaxCastDistance"), 800.0 }, { TEXT("MaxCastDistance"), NaN },
		{ TEXT("CastSpeed"), 0.0 }, { TEXT("CastFlightTimeMin"), 0.0 }, { TEXT("CastFlightTimeMin"), 2.0, TEXT("CastFlightTimeMax"), 1.0 },
		{ TEXT("MaxLineLength"), -1.0 }, { TEXT("MaxLineLength"), NaN }, { TEXT("BiteWaitMin"), -1.0 }, { TEXT("BiteWaitMin"), 8.0, TEXT("BiteWaitMax"), 4.0 },
		{ TEXT("RebiteWaitMin"), 9.0, TEXT("RebiteWaitMax"), 3.0 }, { TEXT("NibblesMin"), -1.0 }, { TEXT("NibblesMin"), 3.0, TEXT("NibblesMax"), 1.0 },
		{ TEXT("NibbleInterval"), 0.0 }, { TEXT("NibbleDuration"), 0.0 }, { TEXT("HookWindow"), 0.0 }, { TEXT("HookWindow"), -0.5 },
		{ TEXT("HookWindow"), NaN }, { TEXT("HookWindow"), std::numeric_limits<double>::infinity() }, { TEXT("HookLatencyGrace"), -0.1 },
		{ TEXT("SpookDelay"), -1.0 }, { TEXT("AutoLandDelay"), -1.0 }, { TEXT("NoBiteHintDelay"), -1.0 }, { TEXT("BobberScale"), 0.0 },
		{ TEXT("LinePixelWidth"), 0.0 }, { TEXT("LineMinWidth"), -1.0 }, { TEXT("LineSag"), -0.1 }, { TEXT("LineSegments"), 0.0 },
		{ TEXT("LineSegments"), 65.0 }, { TEXT("BiteRumbleIntensity"), 1.5 }, { TEXT("BiteRumbleIntensity"), -0.1 }, { TEXT("BiteRumbleDuration"), -1.0 },
		{ TEXT("CastSwingTime"), 0.0 }, { TEXT("BiteDipDepth"), -1.0 },
	};
	ULureFishingComponent* Component = NewObject<ULureFishingComponent>();
	AddExpectedMessagePlain(FLureFishingRules::FallbackWarningMarker, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	for (const FMutation& Mutation : Bad)
	{
		FLureFishingRow Row = Shipped;
		SetField(*this, Struct, &Row, Mutation.Field, Mutation.Value);
		FString Label = FString::Printf(TEXT("%s = %g"), Mutation.Field, Mutation.Value);
		if (Mutation.Field2)
		{
			SetField(*this, Struct, &Row, Mutation.Field2, Mutation.Value2);
			Label += FString::Printf(TEXT(", %s = %g"), Mutation.Field2, Mutation.Value2);
		}
		FString Problem;
		TestFalse(Label + TEXT(": rejected"), Row.Validate(Problem));
		TestFalse(Label + TEXT(": with a reason"), Problem.IsEmpty());
		Component->SetFishingProfile(Row);
		TestTrue(Label + TEXT(": never used (built-in profile)"), Component->IsUsingFallbackProfile());
	}
	const FMutation Edges[] = {
		{ TEXT("HookLatencyGrace"), 0.0 }, { TEXT("NibblesMin"), 0.0, TEXT("NibblesMax"), 0.0 }, { TEXT("LineSegments"), 1.0 }, { TEXT("LineSegments"), 64.0 },
		{ TEXT("BiteRumbleIntensity"), 0.0 }, { TEXT("BiteRumbleIntensity"), 1.0 }, { TEXT("MinCastDistance"), 800.0, TEXT("MaxCastDistance"), 800.0 },
		{ TEXT("BiteWaitMin"), 6.0, TEXT("BiteWaitMax"), 6.0 }, { TEXT("AutoLandDelay"), 0.0 }, { TEXT("SpookDelay"), 0.0 }, { TEXT("LineSag"), 0.0 },
	};
	for (const FMutation& Mutation : Edges)
	{
		FLureFishingRow Row = Shipped;
		SetField(*this, Struct, &Row, Mutation.Field, Mutation.Value);
		FString Label = FString::Printf(TEXT("edge %s = %g"), Mutation.Field, Mutation.Value);
		if (Mutation.Field2)
		{
			SetField(*this, Struct, &Row, Mutation.Field2, Mutation.Value2);
			Label += FString::Printf(TEXT(", %s = %g"), Mutation.Field2, Mutation.Value2);
		}
		FString Problem;
		const bool bValid = Row.Validate(Problem);
		TestTrue(Label + TEXT(": still valid ") + Problem, bValid);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishDataEnumCellsImportAsNamed, "Project.Fishing.QA.Data.EnumCellsImportAsNamed", QAFishing::Flags)
bool FQAFishDataEnumCellsImportAsNamed::RunTest(const FString& Parameters)
{
	// Rules written as names in the CSV reach the game as those values; an unknown name is reported by the import (not a silent 0).
	FString Fishing;
	FString Movement;
	if (!LoadFishingCsv(*this, Fishing) || !LoadMovementCsv(*this, Movement))
	{
		return false;
	}
	for (const ELureEarlyHookRule Rule : { ELureEarlyHookRule::ReelIn, ELureEarlyHookRule::Ignore, ELureEarlyHookRule::Spook })
	{
		const FString Name = StaticEnum<ELureEarlyHookRule>()->GetNameStringByValue(static_cast<int64>(Rule));
		const TStrongObjectPtr<UDataTable> Table(MakeTableChecked(*this, FLureFishingRow::StaticStruct(), WithCell(*this, Fishing, TEXT("Default"), TEXT("EarlyHook"), Name), TEXT("DT_Fishing")));
		const FLureFishingRow* Row = Table->FindRow<FLureFishingRow>(TEXT("Default"), TEXT("QA"), false);
		TestTrue(TEXT("EarlyHook ") + Name + TEXT(" imports as ") + Name, Row && Row->EarlyHook == Rule);
	}
	TArray<FString> Problems;
	const TStrongObjectPtr<UDataTable> BadRule(MakeTable(FLureFishingRow::StaticStruct(), WithCell(*this, Fishing, TEXT("Default"), TEXT("EarlyHook"), TEXT("Spooky")), &Problems));
	TestTrue(FString::Printf(TEXT("EarlyHook 'Spooky' is reported by the import (%d problems)"), Problems.Num()), Problems.Num() > 0);
	const TStrongObjectPtr<UDataTable> Poses(MakeTableChecked(*this, FLureMovementRow::StaticStruct(),
		WithCell(*this, WithCell(*this, Movement, TEXT("Crouch"), TEXT("RodPoseStill"), TEXT("ProneHold")), TEXT("Crouch"), TEXT("RodPoseMoving"), TEXT("Idle")), TEXT("DT_Movement")));
	const FLureMovementRow* Crouch = Poses->FindRow<FLureMovementRow>(TEXT("Crouch"), TEXT("QA"), false);
	TestTrue(TEXT("RodPoseStill ProneHold imports as ProneHold"), Crouch && Crouch->RodPoseStill == EFPArmsPose::ProneHold);
	TestTrue(TEXT("RodPoseMoving Idle imports as Idle"), Crouch && Crouch->RodPoseMoving == EFPArmsPose::Idle);
	Problems.Reset();
	const TStrongObjectPtr<UDataTable> BadPose(MakeTable(FLureMovementRow::StaticStruct(), WithCell(*this, Movement, TEXT("Prone"), TEXT("RodPoseMoving"), TEXT("Tucked")), &Problems));
	TestTrue(FString::Printf(TEXT("RodPoseMoving 'Tucked' is reported by the import (%d problems)"), Problems.Num()), Problems.Num() > 0);
	return true;
}

// =====================================================================================================================
// DT_Movement rod columns (T-006)
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishDataMovementCsvCellsAreTyped, "Project.Fishing.QA.Data.MovementCsvCellsAreTyped", QAFishing::Flags)
bool FQAFishDataMovementCsvCellsAreTyped::RunTest(const FString& Parameters)
{
	FString Csv;
	if (!LoadMovementCsv(*this, Csv))
	{
		return false;
	}
	CheckTypedCsv(*this, Csv, FLureMovementRow::StaticStruct(), TEXT("DT_Movement"));
	TArray<FString> Header;
	TArray<TArray<FString>> Rows;
	SplitCsv(Csv, Header, Rows);
	for (const TCHAR* Column : { TEXT("RodPoseStill"), TEXT("RodPoseMoving"), TEXT("RodMoveSpeedIn"), TEXT("RodMoveSpeedOut"), TEXT("RodStillDelay"),
		TEXT("RodPoseBlendTime"), TEXT("ArmsPitchFollowUp"), TEXT("RodHoldClearance"), TEXT("CanFish") })
	{
		TestTrue(FString::Printf(TEXT("the T-006 column %s is in DT_Movement.csv"), Column), Header.Contains(FString(Column)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishDataMovementRodRules, "Project.Fishing.QA.Data.MovementRodRules", QAFishing::Flags)
bool FQAFishDataMovementRodRules::RunTest(const FString& Parameters)
{
	TArray<FLureMovementRow> Rows;
	if (!ShippedMovementRows(*this, Rows))
	{
		return false;
	}
	int32 NoFishRows = 0;
	for (ELureMovementState State : TEnumRange<ELureMovementState>())
	{
		const FLureMovementRow& Row = RowOf(Rows, State);
		const FString Name = FLureMovementData::GetRowName(State).ToString();
		TestTrue(Name + TEXT(": holding the rod never shows the empty-hands pose"), Row.RodPoseStill != EFPArmsPose::Idle && Row.RodPoseMoving != EFPArmsPose::Idle);
		TestFalse(Name + TEXT(": the still pose is never a tuck (a still player who can fish must hold the rod out)"), Row.CanFish && FLureRodPose::IsTucked(Row.RodPoseStill));
		TestTrue(Name + TEXT(": RodMoveSpeedIn > 0 (a still player never counts as moving)"), Row.RodMoveSpeedIn > 0.f);
		TestTrue(Name + TEXT(": RodMoveSpeedOut <= RodMoveSpeedIn (hysteresis)"), Row.RodMoveSpeedOut <= Row.RodMoveSpeedIn);
		if (FLureRodPose::IsTucked(Row.RodPoseMoving))
		{
			TestTrue(FString::Printf(TEXT("%s: moving at full speed (%.0f) passes RodMoveSpeedIn (%.0f), so moving really tucks"), *Name, Row.MaxSpeed, Row.RodMoveSpeedIn), Row.MaxSpeed > Row.RodMoveSpeedIn);
		}
		TestTrue(Name + TEXT(": ArmsPitchFollowUp in [0, 1]"), Row.ArmsPitchFollowUp >= 0.f && Row.ArmsPitchFollowUp <= 1.f);
		TestTrue(Name + TEXT(": RodStillDelay in [0, 2] s"), Row.RodStillDelay >= 0.f && Row.RodStillDelay <= 2.f);
		TestTrue(Name + TEXT(": RodPoseBlendTime in [0, 1] s"), Row.RodPoseBlendTime >= 0.f && Row.RodPoseBlendTime <= 1.f);
		TestTrue(Name + TEXT(": RodHoldClearance >= 0, and only with a prone hold"), Row.RodHoldClearance >= 0.f && (Row.RodHoldClearance == 0.f || Row.RodPoseStill == EFPArmsPose::ProneHold));
		NoFishRows += Row.CanFish ? 0 : 1;
	}
	TestFalse(TEXT("spec: Sprint can't fish (CanFish False)"), RowOf(Rows, ELureMovementState::Sprint).CanFish);
	TestEqual(TEXT("spec: only Sprint can't fish"), NoFishRows, 1);
	TestTrue(TEXT("Jimmy 2026-09-22: prone fishing is allowed"), RowOf(Rows, ELureMovementState::Prone).CanFish);
	TestNearlyEqual(TEXT("spec: prone arms stay down when looking up (ArmsPitchFollowUp 0)"), RowOf(Rows, ELureMovementState::Prone).ArmsPitchFollowUp, 0.f, 1.0e-6f);
	TestNearlyEqual(TEXT("spec: prone wall check 130 cm"), RowOf(Rows, ELureMovementState::Prone).RodHoldClearance, 130.f, 1.0e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishDataMovementBuiltInRowsEqualCsv, "Project.Fishing.QA.Data.MovementBuiltInRowsEqualCsv", QAFishing::Flags)
bool FQAFishDataMovementBuiltInRowsEqualCsv::RunTest(const FString& Parameters)
{
	// The per-stance fallback rows (DT_Movement missing, as in lanes and until the re-import) equal the CSV in every column, rod ones included.
	TArray<FLureMovementRow> Rows;
	if (!ShippedMovementRows(*this, Rows))
	{
		return false;
	}
	for (ELureMovementState State : TEnumRange<ELureMovementState>())
	{
		const FLureMovementRow BuiltIn = FLureMovementData::GetFallbackRow(State);
		const TArray<FString> Differences = DifferentFields(FLureMovementRow::StaticStruct(), &BuiltIn, &RowOf(Rows, State));
		TestEqual(FString::Printf(TEXT("%s: the built-in row equals the CSV (differs in: %s)"), *FLureMovementData::GetRowName(State).ToString(), *FString::Join(Differences, TEXT(", "))),
			Differences.Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishDataMovementValidateRejectsBadRodColumns, "Project.Fishing.QA.Data.MovementValidateRejectsBadRodColumns", QAFishing::Flags)
bool FQAFishDataMovementValidateRejectsBadRodColumns::RunTest(const FString& Parameters)
{
	TArray<FLureMovementRow> Rows;
	FString Csv;
	if (!ShippedMovementRows(*this, Rows) || !LoadMovementCsv(*this, Csv))
	{
		return false;
	}
	UScriptStruct* Struct = FLureMovementRow::StaticStruct();
	const FMutation Bad[] = {
		{ TEXT("RodMoveSpeedIn"), -1.0 }, { TEXT("RodMoveSpeedIn"), 10.0, TEXT("RodMoveSpeedOut"), 11.0 }, { TEXT("RodStillDelay"), -0.1 },
		{ TEXT("RodStillDelay"), NaN }, { TEXT("RodPoseBlendTime"), -1.0 }, { TEXT("ArmsPitchFollowUp"), 1.5 }, { TEXT("ArmsPitchFollowUp"), -0.1 },
		{ TEXT("ArmsPitchFollowUp"), NaN }, { TEXT("RodHoldClearance"), -1.0 }, { TEXT("RodMoveSpeedOut"), std::numeric_limits<double>::infinity() },
	};
	for (const FMutation& Mutation : Bad)
	{
		FLureMovementRow Row = RowOf(Rows, ELureMovementState::Prone);
		SetField(*this, Struct, &Row, Mutation.Field, Mutation.Value);
		FString Label = FString::Printf(TEXT("Prone %s = %g"), Mutation.Field, Mutation.Value);
		if (Mutation.Field2)
		{
			SetField(*this, Struct, &Row, Mutation.Field2, Mutation.Value2);
			Label += FString::Printf(TEXT(", %s = %g"), Mutation.Field2, Mutation.Value2);
		}
		FString Problem;
		TestFalse(Label + TEXT(": rejected"), Row.Validate(Problem));
	}
	const FMutation Edges[] = { { TEXT("ArmsPitchFollowUp"), 0.0 }, { TEXT("ArmsPitchFollowUp"), 1.0 }, { TEXT("RodHoldClearance"), 0.0 },
		{ TEXT("RodStillDelay"), 0.0 }, { TEXT("RodMoveSpeedIn"), 12.0, TEXT("RodMoveSpeedOut"), 12.0 }, { TEXT("RodPoseBlendTime"), 0.0 } };
	for (const FMutation& Mutation : Edges)
	{
		FLureMovementRow Row = RowOf(Rows, ELureMovementState::Prone);
		SetField(*this, Struct, &Row, Mutation.Field, Mutation.Value);
		if (Mutation.Field2)
		{
			SetField(*this, Struct, &Row, Mutation.Field2, Mutation.Value2);
		}
		FString Problem;
		const bool bValid = Row.Validate(Problem);
		TestTrue(FString::Printf(TEXT("edge Prone %s = %g: still valid %s"), Mutation.Field, Mutation.Value, *Problem), bValid);
	}
	// A table with a bad rod cell: that stance falls back to the built-in row (one warning), the others keep the table's.
	AddExpectedMessagePlain(FLureMovementData::FallbackWarningMarker, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	const TStrongObjectPtr<UDataTable> Table(MakeTableChecked(*this, Struct, WithCell(*this, Csv, TEXT("Prone"), TEXT("ArmsPitchFollowUp"), TEXT("1.5")), TEXT("DT_Movement fixture")));
	TArray<FLureMovementRow> Resolved;
	TArray<FString> Problems;
	const uint8 Mask = FLureMovementData::ResolveRows(Table.Get(), Resolved, Problems);
	TestEqual(TEXT("only the Prone row falls back"), static_cast<int32>(Mask), 1 << static_cast<int32>(ELureMovementState::Prone));
	TestNearlyEqual(TEXT("... to the built-in prone follow-up (0)"), RowOf(Resolved, ELureMovementState::Prone).ArmsPitchFollowUp, 0.f, 1.0e-6f);
	return true;
}

// =====================================================================================================================
// Input actions
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishInputKeysFromSettings, "Project.Fishing.QA.Input.KeysFromSettings", QAFishing::Flags)
bool FQAFishInputKeysFromSettings::RunTest(const FString& Parameters)
{
	// Spec (Controls): Cast = LMB and gamepad RT (settings CastKeys); Hook has no default key (settings HookKeys) so no key drives two actions.
	const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
	const UInputMappingContext* Context = ULureInputSubsystem::GetDefaultMappingContext();
	UInputAction* CastAction = ULureInputSubsystem::GetInputActionByName(TEXT("Cast"));
	UInputAction* HookAction = ULureInputSubsystem::GetInputActionByName(TEXT("Hook"));
	if (!TestNotNull(TEXT("mapping context"), Context) || !TestNotNull(TEXT("Cast"), CastAction) || !TestNotNull(TEXT("Hook"), HookAction))
	{
		return false;
	}
	TestTrue(TEXT("settings: Cast keys are LMB and gamepad RT"), Settings->CastKeys.Contains(EKeys::LeftMouseButton) && Settings->CastKeys.Contains(EKeys::Gamepad_RightTrigger));
	TestEqual(TEXT("settings: Hook has no default key"), Settings->HookKeys.Num(), 0);
	TArray<FKey> CastMapped;
	TArray<FKey> HookMapped;
	for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
	{
		if (Mapping.Action == CastAction)
		{
			CastMapped.Add(Mapping.Key);
		}
		if (Mapping.Action == HookAction)
		{
			HookMapped.Add(Mapping.Key);
		}
		if (Mapping.Action != CastAction && Mapping.Action != HookAction)
		{
			TestFalse(FString::Printf(TEXT("%s's key %s is not a fishing key"), *GetNameSafe(Mapping.Action), *Mapping.Key.ToString()), Settings->CastKeys.Contains(Mapping.Key));
		}
	}
	TestEqual(TEXT("Cast is mapped to exactly the settings' keys"), CastMapped.Num(), Settings->CastKeys.Num());
	for (const FKey& Key : Settings->CastKeys)
	{
		TestTrue(TEXT("Cast mapped to ") + Key.ToString(), CastMapped.Contains(Key));
	}
	TestEqual(TEXT("Hook is mapped to exactly the settings' keys (none)"), HookMapped.Num(), Settings->HookKeys.Num());
	TestFalse(TEXT("F8 (playtest feedback) is not a fishing key"), CastMapped.Contains(EKeys::F8) || HookMapped.Contains(EKeys::F8));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAFishInputBoundActionsDriveFishing, "Project.Fishing.QA.Input.BoundActionsDriveFishing", QAFishing::Flags)
bool FQAFishInputBoundActionsDriveFishing::RunTest(const FString& Parameters)
{
	// A locally possessed player: Cast started = charge, Cast completed = cast, Hook started during a bite = hooked.
	FLureFishingRow Shipped;
	FScene Scene;
	if (!ShippedFishingRow(*this, Shipped) || !Scene.Create(*this))
	{
		return false;
	}
	Scene.AddShoreSpot();
	ALurePlayerCharacter* Character = Scene.Spawn(*this);
	ULureFishingComponent* Fishing = Scene.SetUpFishing(*this, Character, FlowProfile(Shipped, 0.5f, 1.5f));
	if (!Fishing)
	{
		return false;
	}
	Scene.PossessLocally(Character);
	Scene.Tick(10);
	const UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(Character->InputComponent);
	UInputAction* CastAction = ULureInputSubsystem::GetInputActionByName(TEXT("Cast"));
	UInputAction* HookAction = ULureInputSubsystem::GetInputActionByName(TEXT("Hook"));
	if (!TestNotNull(TEXT("enhanced input component"), Input) || !CastAction || !HookAction)
	{
		return false;
	}
	struct FInstance : public FInputActionInstance
	{
		FInstance(const UInputAction* Action, ETriggerEvent Event) : FInputActionInstance(Action)
		{
			TriggerEvent = Event;
			Value = FInputActionValue(Event != ETriggerEvent::Completed);
		}
	};
	auto Fire = [Input](const UInputAction* Action, ETriggerEvent Event)
	{
		int32 Ran = 0;
		const FInstance Instance(Action, Event);
		for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Input->GetActionEventBindings())
		{
			if (Binding && Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event)
			{
				Binding->Execute(Instance);
				++Ran;
			}
		}
		return Ran;
	};
	TestTrue(TEXT("Cast started is bound"), Fire(CastAction, ETriggerEvent::Started) > 0);
	TestTrue(TEXT("... and starts charging"), Fishing->IsCharging());
	Scene.Tick(20);
	TestTrue(TEXT("Cast completed is bound"), Fire(CastAction, ETriggerEvent::Completed) > 0);
	TestEqual(TEXT("... and casts"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Casting));
	if (!TestTrue(TEXT("a bite"), Scene.TickUntil([Fishing]() { return Fishing->GetFishingState() == ELureFishingState::Biting; }, 240)))
	{
		return false;
	}
	TestTrue(TEXT("Hook started is bound"), Fire(HookAction, ETriggerEvent::Started) > 0);
	TestEqual(TEXT("... and hooks the bite"), StateName(Fishing->GetFishingState()), StateName(ELureFishingState::Hooked));
	TestEqual(TEXT("Hook does nothing on release (no Completed binding)"), Fire(HookAction, ETriggerEvent::Completed), 0);
	return true;
}

} // namespace QAFishing

#endif // WITH_DEV_AUTOMATION_TESTS

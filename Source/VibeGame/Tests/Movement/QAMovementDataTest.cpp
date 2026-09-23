// Lure T-004 QA (qa-engineer): DT_Movement data validation, row resolution/fallbacks, settings and game mode.
// Project.Movement.QA.Data.*, .Fallback.*, .Config.*  (QA design groups V, B, C)

#include "Tests/Movement/QAMovementTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureCharacterSettings.h"
#include "Character/LurePlayerCharacter.h"
#include "Engine/DataTable.h"
#include "Game/LureGameMode.h"
#include "GameMapsSettings.h"
#include "Misc/PackageName.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"
#include <limits>

// =====================================================================================================================
// V: data validation of data/tables/DT_Movement.csv
// =====================================================================================================================

namespace QAMovementData
{
	/** Runs one design rule on the shipped CSV; returns false if the CSV didn't load. */
	bool CheckShippedRule(FAutomationTestBase& Test, QAM::ERule Rule, const TCHAR* RuleName)
	{
		UDataTable* Table = QAM::ShippedTable(Test);
		if (!Table)
		{
			return false;
		}
		uint8 FallbackMask = 0;
		const TArray<FLureMovementRow> Rows = QAM::Resolve(Table, &FallbackMask);
		Test.TestEqual(TEXT("every shipped row is used (none falls back)"), static_cast<int32>(FallbackMask), 0);
		for (const FString& Broken : QAM::CheckRule(Rule, Rows))
		{
			Test.AddError(FString::Printf(TEXT("%s: %s"), RuleName, *Broken));
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveDataCsvParsesCleanly, "Project.Movement.QA.Data.CsvParsesCleanly", QAMovement::Flags)
bool FQAMoveDataCsvParsesCleanly::RunTest(const FString& Parameters)
{
	FString Csv;
	if (!TestTrue(FString::Printf(TEXT("%s exists"), *QAM::ShippedCsvPath()), QAM::LoadShippedCsv(Csv)))
	{
		return false;
	}
	TArray<FString> Problems;
	QAM::MakeTable(Csv, &Problems);
	TestEqual(FString::Printf(TEXT("import problems (missing/unknown columns, duplicates, bad cells): %s"), *FString::Join(Problems, TEXT(" | "))), Problems.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveDataAllFourStancesPresent, "Project.Movement.QA.Data.AllFourStancesPresent", QAMovement::Flags)
bool FQAMoveDataAllFourStancesPresent::RunTest(const FString& Parameters)
{
	UDataTable* Table = QAM::ShippedTable(*this);
	if (!Table)
	{
		return false;
	}
	// T-026 added the Swim and SwimSprint rows (the test name predates them).
	TArray<FName> Names = Table->GetRowNames();
	for (const TCHAR* Required : { TEXT("Stand"), TEXT("Sprint"), TEXT("Crouch"), TEXT("Prone"), TEXT("Swim"), TEXT("SwimSprint") })
	{
		TestTrue(FString::Printf(TEXT("row %s present"), Required), Names.Contains(FName(Required)));
	}
	for (const FName& Name : Names)
	{
		const bool bKnown = Name == TEXT("Stand") || Name == TEXT("Sprint") || Name == TEXT("Crouch") || Name == TEXT("Prone")
			|| Name == TEXT("Swim") || Name == TEXT("SwimSprint");
		TestTrue(FString::Printf(TEXT("row '%s' is a known stance (typo?)"), *Name.ToString()), bKnown);
	}
	TestEqual(TEXT("exactly 6 rows"), Names.Num(), 6);
	// QA review of the T-026 fixture edit: the list above must stay in step with the movement states the code resolves.
	TestEqual(TEXT("one row per ELureMovementState"), Names.Num(), static_cast<int32>(FLureMovementData::NumStates));
	for (const ELureMovementState State : TEnumRange<ELureMovementState>())
	{
		TestTrue(FString::Printf(TEXT("state row %s present"), *FLureMovementData::GetRowName(State).ToString()), Names.Contains(FLureMovementData::GetRowName(State)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveDataNoDuplicateRowNamesRawScan, "Project.Movement.QA.Data.NoDuplicateRowNamesRawScan", QAMovement::Flags)
bool FQAMoveDataNoDuplicateRowNamesRawScan::RunTest(const FString& Parameters)
{
	FString Csv;
	if (!TestTrue(TEXT("CSV loads"), QAM::LoadShippedCsv(Csv)))
	{
		return false;
	}
	TArray<FString> Lines;
	Csv.ParseIntoArrayLines(Lines, /*CullEmpty*/ true);
	TSet<FString> Seen;
	for (int32 Index = 1; Index < Lines.Num(); ++Index) // line 0 is the header
	{
		FString Name;
		if (!Lines[Index].Split(TEXT(","), &Name, nullptr))
		{
			Name = Lines[Index];
		}
		Name = Name.TrimStartAndEnd().TrimQuotes().ToLower(); // FName is case-insensitive: "Prone" and "prone" would merge
		if (Name.IsEmpty())
		{
			continue;
		}
		bool bAlready = false;
		Seen.Add(Name, &bAlready);
		TestFalse(FString::Printf(TEXT("row name '%s' appears once (case-insensitive)"), *Name), bAlready);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveDataNumericCellsAreNumbers, "Project.Movement.QA.Data.NumericCellsAreNumbers", QAMovement::Flags)
bool FQAMoveDataNumericCellsAreNumbers::RunTest(const FString& Parameters)
{
	// QA finding: UDataTable's CSV import turns text in a number cell (e.g. "fast" or "0.2x") into 0 WITHOUT reporting a problem,
	// and 0 is a valid value for TransitionTime, NoiseMultiplier, JumpZVelocity and the bob columns. So check the raw text.
	FString Csv;
	if (!TestTrue(TEXT("CSV loads"), QAM::LoadShippedCsv(Csv)))
	{
		return false;
	}
	TArray<FString> Lines;
	Csv.ParseIntoArrayLines(Lines, /*CullEmpty*/ true);
	if (!TestTrue(TEXT("a header and rows"), Lines.Num() > 1))
	{
		return false;
	}
	TArray<FString> Header;
	Lines[0].ParseIntoArray(Header, TEXT(","), /*CullEmpty*/ false);
	for (int32 Line = 1; Line < Lines.Num(); ++Line)
	{
		TArray<FString> Cells;
		Lines[Line].ParseIntoArray(Cells, TEXT(","), /*CullEmpty*/ false);
		TestEqual(FString::Printf(TEXT("line %d has one cell per column"), Line + 1), Cells.Num(), Header.Num());
		for (int32 Column = 1; Column < FMath::Min(Cells.Num(), Header.Num()); ++Column)
		{
			const FString Name = Header[Column].TrimStartAndEnd();
			const FString Cell = Cells[Column].TrimStartAndEnd().TrimQuotes();
			const FString Where = FString::Printf(TEXT("%s.%s = '%s'"), *Cells[0].TrimStartAndEnd(), *Name, *Cell);
			// T-006 (unreal-engineer): bool and enum columns by reflection (CanJump, CanFish, RodPoseStill, RodPoseMoving).
			const FProperty* Property = FLureMovementRow::StaticStruct()->FindPropertyByName(FName(*Name));
			const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property);
			if (Name == TEXT("CanJump") || CastField<FBoolProperty>(Property))
			{
				TestTrue(Where + TEXT(" is True or False"), Cell.Equals(TEXT("True"), ESearchCase::IgnoreCase) || Cell.Equals(TEXT("False"), ESearchCase::IgnoreCase));
			}
			else if (EnumProperty && EnumProperty->GetEnum())
			{
				// QA (T-006 review): exactly one of the authored names. GetIndexByNameString alone also accepts the generated
				// "<Enum>_MAX" entry (which imports as an invalid pose) and other spellings; require the exact short name.
				const UEnum* Enum = EnumProperty->GetEnum();
				const int32 NumAuthored = Enum->NumEnums() - (Enum->ContainsExistingMax() ? 1 : 0);
				bool bAuthored = false;
				for (int32 Index = 0; Index < NumAuthored; ++Index)
				{
					bAuthored |= Enum->GetNameStringByIndex(Index).Equals(Cell, ESearchCase::CaseSensitive);
				}
				TestTrue(Where + TEXT(" is exactly one of the enum's names (never _MAX)"), bAuthored);
			}
			else
			{
				TestTrue(Where + TEXT(" is a number"), !Cell.IsEmpty() && FCString::IsNumeric(*Cell));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveDataSpeedsPositiveAndSane, "Project.Movement.QA.Data.SpeedsPositiveAndSane", QAMovement::Flags)
bool FQAMoveDataSpeedsPositiveAndSane::RunTest(const FString& Parameters)
{
	return QAMovementData::CheckShippedRule(*this, QAM::ERule::SpeedsPositiveAndSane, TEXT("SpeedsPositiveAndSane"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveDataSpeedOrdering, "Project.Movement.QA.Data.SpeedOrdering", QAMovement::Flags)
bool FQAMoveDataSpeedOrdering::RunTest(const FString& Parameters)
{
	return QAMovementData::CheckShippedRule(*this, QAM::ERule::SpeedOrdering, TEXT("SpeedOrdering"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveDataCapsuleHeightOrdering, "Project.Movement.QA.Data.CapsuleHeightOrdering", QAMovement::Flags)
bool FQAMoveDataCapsuleHeightOrdering::RunTest(const FString& Parameters)
{
	return QAMovementData::CheckShippedRule(*this, QAM::ERule::CapsuleHeightOrdering, TEXT("CapsuleHeightOrdering"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveDataEyeHeightOrdering, "Project.Movement.QA.Data.EyeHeightOrdering", QAMovement::Flags)
bool FQAMoveDataEyeHeightOrdering::RunTest(const FString& Parameters)
{
	return QAMovementData::CheckShippedRule(*this, QAM::ERule::EyeHeightOrdering, TEXT("EyeHeightOrdering"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveDataRadiusValid, "Project.Movement.QA.Data.RadiusValid", QAMovement::Flags)
bool FQAMoveDataRadiusValid::RunTest(const FString& Parameters)
{
	return QAMovementData::CheckShippedRule(*this, QAM::ERule::RadiusValid, TEXT("RadiusValid"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveDataEyeInsideCapsule, "Project.Movement.QA.Data.EyeInsideCapsule", QAMovement::Flags)
bool FQAMoveDataEyeInsideCapsule::RunTest(const FString& Parameters)
{
	return QAMovementData::CheckShippedRule(*this, QAM::ERule::EyeInsideCapsule, TEXT("EyeInsideCapsule"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveDataProneFits60cmGap, "Project.Movement.QA.Data.ProneFits60cmGap", QAMovement::Flags)
bool FQAMoveDataProneFits60cmGap::RunTest(const FString& Parameters)
{
	return QAMovementData::CheckShippedRule(*this, QAM::ERule::ProneFits60cmGap, TEXT("ProneFits60cmGap"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveDataCrouchDoesNotFit60cmGap, "Project.Movement.QA.Data.CrouchDoesNotFit60cmGap", QAMovement::Flags)
bool FQAMoveDataCrouchDoesNotFit60cmGap::RunTest(const FString& Parameters)
{
	return QAMovementData::CheckShippedRule(*this, QAM::ERule::CrouchDoesNotFit60cmGap, TEXT("CrouchDoesNotFit60cmGap"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveDataStandHeightHumanScale, "Project.Movement.QA.Data.StandHeightHumanScale", QAMovement::Flags)
bool FQAMoveDataStandHeightHumanScale::RunTest(const FString& Parameters)
{
	return QAMovementData::CheckShippedRule(*this, QAM::ERule::StandHeightHumanScale, TEXT("StandHeightHumanScale"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveDataTransitionTimeRange, "Project.Movement.QA.Data.TransitionTimeRange", QAMovement::Flags)
bool FQAMoveDataTransitionTimeRange::RunTest(const FString& Parameters)
{
	return QAMovementData::CheckShippedRule(*this, QAM::ERule::TransitionTimeRange, TEXT("TransitionTimeRange"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveDataNoiseMultiplierOrdering, "Project.Movement.QA.Data.NoiseMultiplierOrdering", QAMovement::Flags)
bool FQAMoveDataNoiseMultiplierOrdering::RunTest(const FString& Parameters)
{
	return QAMovementData::CheckShippedRule(*this, QAM::ERule::NoiseMultiplierOrdering, TEXT("NoiseMultiplierOrdering"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveDataJumpFlags, "Project.Movement.QA.Data.JumpFlags", QAMovement::Flags)
bool FQAMoveDataJumpFlags::RunTest(const FString& Parameters)
{
	return QAMovementData::CheckShippedRule(*this, QAM::ERule::JumpFlags, TEXT("JumpFlags"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveDataGapMinimumsReported, "Project.Movement.QA.Data.GapMinimumsReported", QAMovement::Flags)
bool FQAMoveDataGapMinimumsReported::RunTest(const FString& Parameters)
{
	UDataTable* Table = QAM::ShippedTable(*this);
	if (!Table)
	{
		return false;
	}
	const TArray<FLureMovementRow> Rows = QAM::Resolve(Table);
	// Level-design numbers (A12): crawl-only gaps sit between the prone and crouch minimums; the standard is 60 cm.
	const float Prone = QAM::Clear(QAM::RowOf(Rows, ELureMovementState::Prone)) + QAM::MaxFloorDist;
	const float Crouch = QAM::Clear(QAM::RowOf(Rows, ELureMovementState::Crouch)) + QAM::MaxFloorDist;
	const float Stand = QAM::Clear(QAM::RowOf(Rows, ELureMovementState::Stand)) + QAM::MaxFloorDist;
	AddInfo(FString::Printf(TEXT("Minimum walkable gap (floor to ceiling, incl. %.1f cm floor gap): prone %.1f cm, crouch %.1f cm, stand %.1f cm. Crawl-only gaps: [%.1f, %.1f) cm."),
		QAM::MaxFloorDist, Prone, Crouch, Stand, Prone, Crouch));
	TestTrue(TEXT("the crawl-only window is not empty"), Prone < Crouch);
	return true;
}

// =====================================================================================================================
// B: row resolution and fallbacks (a Warning, never an Error; A19/A20)
// =====================================================================================================================

namespace QAMovementData
{
	const TCHAR* Marker()
	{
		return FLureMovementData::FallbackWarningMarker;
	}

	/** Fixture A with one change, applied to a spawned character. */
	ALurePlayerCharacter* SpawnWithRows(FAutomationTestBase& Test, QAM::FWorld& World, const TArray<QAM::FRowSpec>& Rows, bool bExpectImportProblems = false)
	{
		TArray<FString> Problems;
		UDataTable* Table = QAM::MakeTable(QAM::MakeCsv(Rows), &Problems);
		if (!bExpectImportProblems && Problems.Num() > 0)
		{
			Test.AddError(FString::Printf(TEXT("fixture import problems: %s"), *FString::Join(Problems, TEXT(" | "))));
		}
		ALurePlayerCharacter* Character = World.Spawn(Test, FVector::ZeroVector, Table);
		if (Character)
		{
			World.Tick(QAM::SettleFrames);
		}
		return Character;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveFallbackNoTableUsesBuiltInRows, "Project.Movement.QA.Fallback.NoTableUsesBuiltInRows", QAMovement::Flags)
bool FQAMoveFallbackNoTableUsesBuiltInRows::RunTest(const FString& Parameters)
{
	// Exactly one warning for the resolve: not one per tick and not one per stance change.
	AddExpectedMessagePlain(QAMovementData::Marker(), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.Spawn(*this, FVector::ZeroVector, nullptr);
	if (!Character)
	{
		return false;
	}
	World.Tick(QAM::SettleFrames);
	ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	for (ELureMovementState State : TEnumRange<ELureMovementState>())
	{
		TestTrue(FString::Printf(TEXT("%s uses the fallback row"), *QAM::StateName(State)), Movement->IsUsingFallbackRow(State));
		TestTrue(FString::Printf(TEXT("%s resolved row == built-in fallback"), *QAM::StateName(State)),
			QAM::RowsEqual(Movement->GetMovementRow(State), ULureCharacterMovementComponent::GetFallbackRow(State)));
	}
	// Stance changes and ticks must not repeat the warning.
	QAM::EnterStance(World, Character, ELureStance::Crouch);
	QAM::EnterStance(World, Character, ELureStance::Prone);
	QAM::EnterStance(World, Character, ELureStance::Stand);
	World.Tick(30);
	TestEqual(TEXT("character still stands on fallback data"), Character->GetStance(), ELureStance::Stand);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveFallbackSettingsPathMatchesEnvironment, "Project.Movement.QA.Fallback.SettingsPathMatchesEnvironment", QAMovement::Flags)
bool FQAMoveFallbackSettingsPathMatchesEnvironment::RunTest(const FString& Parameters)
{
	// The game's own path (settings soft pointer). In a lane the asset never exists: one Warning (not an Error), fallback rows.
	// Once the editor-operator has imported /Game/Data/DT_Movement: no warning, and the rows equal the CSV.
	const FString PackageName = GetDefault<ULureCharacterSettings>()->MovementTable.ToSoftObjectPath().GetLongPackageName();
	const bool bAssetExists = !PackageName.IsEmpty() && FPackageName::DoesPackageExist(PackageName);
	if (!bAssetExists)
	{
		AddExpectedMessagePlain(QAMovementData::Marker(), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	}
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.Spawn(*this, FVector::ZeroVector, nullptr, /*bApplyTable*/ false);
	if (!Character)
	{
		return false;
	}
	World.Tick(QAM::SettleFrames);
	const ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	if (bAssetExists)
	{
		AddInfo(FString::Printf(TEXT("%s is imported: checking it is used"), *PackageName));
		UDataTable* Csv = QAM::ShippedTable(*this);
		const TArray<FLureMovementRow> Expected = QAM::Resolve(Csv);
		for (ELureMovementState State : TEnumRange<ELureMovementState>())
		{
			TestFalse(FString::Printf(TEXT("%s does not fall back"), *QAM::StateName(State)), Movement->IsUsingFallbackRow(State));
			TestTrue(FString::Printf(TEXT("%s row == CSV"), *QAM::StateName(State)), QAM::RowsEqual(Movement->GetMovementRow(State), QAM::RowOf(Expected, State)));
		}
	}
	else
	{
		AddInfo(FString::Printf(TEXT("%s is not imported here (lane): expecting one fallback warning"), *PackageName));
		for (ELureMovementState State : TEnumRange<ELureMovementState>())
		{
			TestTrue(FString::Printf(TEXT("%s falls back"), *QAM::StateName(State)), Movement->IsUsingFallbackRow(State));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveFallbackBuiltInRowsPassValidation, "Project.Movement.QA.Fallback.BuiltInRowsPassValidation", QAMovement::Flags)
bool FQAMoveFallbackBuiltInRowsPassValidation::RunTest(const FString& Parameters)
{
	TArray<FLureMovementRow> Rows;
	for (ELureMovementState State : TEnumRange<ELureMovementState>())
	{
		Rows.Add(ULureCharacterMovementComponent::GetFallbackRow(State));
		FString Problem;
		TestTrue(FString::Printf(TEXT("%s fallback row passes the runtime check (%s)"), *QAM::StateName(State), *Problem), Rows.Last().Validate(Problem));
	}
	for (const FString& Broken : QAM::CheckAllRules(Rows))
	{
		AddError(FString::Printf(TEXT("built-in fallback rows break a design rule: %s"), *Broken));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveFallbackMissingRowFallsBackPerStance, "Project.Movement.QA.Fallback.MissingRowFallsBackPerStance", QAMovement::Flags)
bool FQAMoveFallbackMissingRowFallsBackPerStance::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(QAMovementData::Marker(), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TArray<QAM::FRowSpec> Rows = QAM::FixtureARows();
	Rows.RemoveAll([](const QAM::FRowSpec& Spec) { return Spec.Name == TEXT("Prone"); });
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementData::SpawnWithRows(*this, World, Rows);
	if (!Character)
	{
		return false;
	}
	const ULureCharacterMovementComponent* Movement = Character->GetLureMovement();
	TestTrue(TEXT("Prone falls back"), Movement->IsUsingFallbackRow(ELureMovementState::Prone));
	TestTrue(TEXT("Prone row == built-in"), QAM::RowsEqual(Movement->GetMovementRow(ELureMovementState::Prone), ULureCharacterMovementComponent::GetFallbackRow(ELureMovementState::Prone)));
	TestFalse(TEXT("Stand uses the table"), Movement->IsUsingFallbackRow(ELureMovementState::Stand));
	TestFalse(TEXT("Sprint uses the table"), Movement->IsUsingFallbackRow(ELureMovementState::Sprint));
	TestFalse(TEXT("Crouch uses the table"), Movement->IsUsingFallbackRow(ELureMovementState::Crouch));
	TestNearlyEqual(TEXT("Stand MaxSpeed from the table"), Movement->GetMovementRow(ELureMovementState::Stand).MaxSpeed, 311.f, 0.01f);
	TestNearlyEqual(TEXT("Crouch MaxSpeed from the table"), Movement->GetMovementRow(ELureMovementState::Crouch).MaxSpeed, 173.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveFallbackNonPositiveSpeedRejected, "Project.Movement.QA.Fallback.NonPositiveSpeedRejected", QAMovement::Flags)
bool FQAMoveFallbackNonPositiveSpeedRejected::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(QAMovementData::Marker(), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	for (const float BadSpeed : { 0.f, -50.f })
	{
		TArray<QAM::FRowSpec> Rows = QAM::FixtureARows();
		QAM::FindSpec(Rows, TEXT("Stand"))->MaxSpeed = BadSpeed;
		QAM::FWorld World;
		if (!World.Create(*this))
		{
			return false;
		}
		ALurePlayerCharacter* Character = QAMovementData::SpawnWithRows(*this, World, Rows);
		if (!Character)
		{
			return false;
		}
		const FString Label = FString::Printf(TEXT("Stand.MaxSpeed = %.0f"), BadSpeed);
		TestTrue(Label + TEXT(": Stand falls back"), Character->GetLureMovement()->IsUsingFallbackRow(ELureMovementState::Stand));
		TestTrue(Label + TEXT(": runtime max speed > 0"), QAM::MaxSpeedNow(Character) > 0.f);
		World.TickMoving(Character, 30);
		TestTrue(Label + TEXT(": the character can still walk"), QAM::HorizontalSpeed(Character) > 50.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveFallbackNonNumericCellHandled, "Project.Movement.QA.Fallback.NonNumericCellHandled", QAMovement::Flags)
bool FQAMoveFallbackNonNumericCellHandled::RunTest(const FString& Parameters)
{
	// "fast" in a number cell: either the import reports it, or it reads as 0 (then the row must fall back).
	FString Csv = QAM::MakeCsv(QAM::FixtureARows());
	Csv = Csv.Replace(TEXT("Crouch,173.0000"), TEXT("Crouch,fast"));
	TArray<FString> Problems;
	UDataTable* Table = QAM::MakeTable(Csv, &Problems);
	uint8 Mask = 0;
	const TArray<FLureMovementRow> Rows = QAM::Resolve(Table, &Mask);
	const FLureMovementRow& Crouch = QAM::RowOf(Rows, ELureMovementState::Crouch);
	AddInfo(FString::Printf(TEXT("import problems: %d; resolved Crouch.MaxSpeed %.1f; fallback mask 0x%02x"), Problems.Num(), Crouch.MaxSpeed, Mask));
	TestTrue(TEXT("the bad cell is reported or the row falls back"), Problems.Num() > 0 || (Mask & (1u << static_cast<int32>(ELureMovementState::Crouch))) != 0);
	TestTrue(TEXT("resolved Crouch.MaxSpeed is usable (> 0)"), FMath::IsFinite(Crouch.MaxSpeed) && Crouch.MaxSpeed > 0.f);
	TestNearlyEqual(TEXT("other rows keep their values (Stand)"), QAM::RowOf(Rows, ELureMovementState::Stand).MaxSpeed, 311.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveFallbackHalfHeightBelowRadiusHandled, "Project.Movement.QA.Fallback.HalfHeightBelowRadiusHandled", QAMovement::Flags)
bool FQAMoveFallbackHalfHeightBelowRadiusHandled::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(QAMovementData::Marker(), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TArray<QAM::FRowSpec> Rows = QAM::FixtureARows();
	QAM::FRowSpec* Prone = QAM::FindSpec(Rows, TEXT("Prone"));
	Prone->HalfHeight = 20.f; // < radius 25
	Prone->EyeHeight = 15.f;
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementData::SpawnWithRows(*this, World, Rows);
	if (!Character || !TestTrue(TEXT("goes prone"), QAM::EnterStance(World, Character, ELureStance::Prone)))
	{
		return false;
	}
	float Radius = 0.f;
	float HalfHeight = 0.f;
	QAM::GetCapsule(Character, Radius, HalfHeight);
	TestTrue(FString::Printf(TEXT("prone capsule half height %.2f >= radius %.2f"), HalfHeight, Radius), HalfHeight >= Radius - 0.01f);
	TestTrue(TEXT("capsule matches the resolved Prone row"), QAM::CapsuleMatches(Character, Character->GetLureMovement()->GetMovementRow(ELureMovementState::Prone)));
	TestFalse(TEXT("no penetration"), QAM::IsPenetrating(Character));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveFallbackNegativeTransitionTimeHandled, "Project.Movement.QA.Fallback.NegativeTransitionTimeHandled", QAMovement::Flags)
bool FQAMoveFallbackNegativeTransitionTimeHandled::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(QAMovementData::Marker(), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TArray<QAM::FRowSpec> Rows = QAM::FixtureARows();
	QAM::FindSpec(Rows, TEXT("Crouch"))->TransitionTime = -1.f;
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementData::SpawnWithRows(*this, World, Rows);
	if (!Character)
	{
		return false;
	}
	TestTrue(TEXT("resolved Crouch.TransitionTime >= 0"), Character->GetLureMovement()->GetMovementRow(ELureMovementState::Crouch).TransitionTime >= 0.f);
	Character->RequestStance(ELureStance::Crouch);
	bool bAllFinite = true;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		World.Tick(1);
		bAllFinite &= FMath::IsFinite(QAM::EyeAboveFeet(Character)) && FMath::IsFinite(Character->GetCurrentEyeHeight());
	}
	TestTrue(TEXT("eye height is never NaN/Inf"), bAllFinite);
	TestEqual(TEXT("crouched"), Character->GetStance(), ELureStance::Crouch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveFallbackWrongRowStructFallsBack, "Project.Movement.QA.Fallback.WrongRowStructFallsBack", QAMovement::Flags)
bool FQAMoveFallbackWrongRowStructFallsBack::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(QAMovementData::Marker(), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
	Table->RowStruct = FTableRowBase::StaticStruct();
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = World.Spawn(*this, FVector::ZeroVector, Table);
	if (!Character)
	{
		return false;
	}
	World.Tick(QAM::SettleFrames);
	for (ELureMovementState State : TEnumRange<ELureMovementState>())
	{
		TestTrue(FString::Printf(TEXT("%s falls back"), *QAM::StateName(State)), Character->GetLureMovement()->IsUsingFallbackRow(State));
	}
	TestTrue(TEXT("still walks"), QAM::MaxSpeedNow(Character) > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveFallbackEmptyTableFallsBack, "Project.Movement.QA.Fallback.EmptyTableFallsBack", QAMovement::Flags)
bool FQAMoveFallbackEmptyTableFallsBack::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(QAMovementData::Marker(), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementData::SpawnWithRows(*this, World, TArray<QAM::FRowSpec>());
	if (!Character)
	{
		return false;
	}
	for (ELureMovementState State : TEnumRange<ELureMovementState>())
	{
		TestTrue(FString::Printf(TEXT("%s falls back"), *QAM::StateName(State)), Character->GetLureMovement()->IsUsingFallbackRow(State));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveFallbackUnknownExtraRowIgnored, "Project.Movement.QA.Fallback.UnknownExtraRowIgnored", QAMovement::Flags)
bool FQAMoveFallbackUnknownExtraRowIgnored::RunTest(const FString& Parameters)
{
	TArray<QAM::FRowSpec> Rows = QAM::FixtureARows();
	Rows.Add({ TEXT("Swimm"), 120.f, 1000.f, 40.f, 30.f, 50.f, 0.3f, 0.3f, 0.f, false }); // a typo row ("Swim" is a real row since T-026)
	uint8 Mask = 0xFF;
	const TArray<FLureMovementRow> Resolved = QAM::Resolve(QAM::MakeTable(*this, Rows), &Mask);
	TestEqual(TEXT("no stance falls back because of an extra row"), static_cast<int32>(Mask), 0);
	TestNearlyEqual(TEXT("Stand still from the table"), QAM::RowOf(Resolved, ELureMovementState::Stand).MaxSpeed, 311.f, 0.01f);
	TestNearlyEqual(TEXT("Prone still from the table"), QAM::RowOf(Resolved, ELureMovementState::Prone).MaxSpeed, 89.f, 0.01f);
	// The typo row sits next to the real "Swim" row: Swim must still come from its own row, not from "Swimm".
	TestNearlyEqual(TEXT("Swim from its own row, not the typo row"), QAM::RowOf(Resolved, ELureMovementState::Swim).MaxSpeed, 151.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveFallbackNonFiniteValuesRejected, "Project.Movement.QA.Fallback.NonFiniteValuesRejected", QAMovement::Flags)
bool FQAMoveFallbackNonFiniteValuesRejected::RunTest(const FString& Parameters)
{
	UDataTable* Table = QAM::FixtureA(*this);
	FLureMovementRow* Stand = Table->FindRow<FLureMovementRow>(TEXT("Stand"), TEXT("QA"));
	FLureMovementRow* Crouch = Table->FindRow<FLureMovementRow>(TEXT("Crouch"), TEXT("QA"));
	if (!TestNotNull(TEXT("Stand row"), Stand) || !TestNotNull(TEXT("Crouch row"), Crouch))
	{
		return false;
	}
	Stand->MaxSpeed = std::numeric_limits<float>::infinity();
	Crouch->EyeHeight = std::numeric_limits<float>::quiet_NaN();
	uint8 Mask = 0;
	const TArray<FLureMovementRow> Rows = QAM::Resolve(Table, &Mask);
	TestTrue(TEXT("Stand (inf speed) falls back"), (Mask & (1u << static_cast<int32>(ELureMovementState::Stand))) != 0);
	TestTrue(TEXT("Crouch (NaN eye) falls back"), (Mask & (1u << static_cast<int32>(ELureMovementState::Crouch))) != 0);
	for (const FLureMovementRow& Row : Rows)
	{
		TestTrue(TEXT("resolved values are finite"), FMath::IsFinite(Row.MaxSpeed) && FMath::IsFinite(Row.EyeHeight));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveFallbackHeightsOutOfOrderHandled, "Project.Movement.QA.Fallback.HeightsOutOfOrderHandled", QAMovement::Flags)
bool FQAMoveFallbackHeightsOutOfOrderHandled::RunTest(const FString& Parameters)
{
	// Crouch taller than Stand: whatever the policy, no crash, no penetration, feet stay planted.
	TArray<QAM::FRowSpec> Rows = QAM::FixtureARows();
	QAM::FindSpec(Rows, TEXT("Crouch"))->HalfHeight = 95.f;
	QAM::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}
	ALurePlayerCharacter* Character = QAMovementData::SpawnWithRows(*this, World, Rows);
	if (!Character)
	{
		return false;
	}
	const float Feet = QAM::FeetZ(Character);
	Character->RequestStance(ELureStance::Crouch);
	World.Tick(QAM::SettleFrames);
	TestFalse(TEXT("crouch: no penetration"), QAM::IsPenetrating(Character));
	TestTrue(TEXT("crouch: feet planted"), FMath::Abs(QAM::FeetZ(Character) - Feet) <= QAM::FeetTolerance);
	Character->RequestStance(ELureStance::Stand);
	World.Tick(QAM::SettleFrames);
	TestFalse(TEXT("stand: no penetration"), QAM::IsPenetrating(Character));
	TestTrue(TEXT("stand: feet planted"), FMath::Abs(QAM::FeetZ(Character) - Feet) <= QAM::FeetTolerance);
	TestEqual(TEXT("back to Stand"), Character->GetStance(), ELureStance::Stand);
	return true;
}

// =====================================================================================================================
// C: settings and game mode
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveConfigSettingsPointToDTMovement, "Project.Movement.QA.Config.SettingsPointToDTMovement", QAMovement::Flags)
bool FQAMoveConfigSettingsPointToDTMovement::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Lure Character settings: MovementTable"), GetDefault<ULureCharacterSettings>()->MovementTable.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Data/DT_Movement.DT_Movement")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveConfigGlobalDefaultGameModeIsLure, "Project.Movement.QA.Config.GlobalDefaultGameModeIsLure", QAMovement::Flags)
bool FQAMoveConfigGlobalDefaultGameModeIsLure::RunTest(const FString& Parameters)
{
	const FString Path = UGameMapsSettings::GetGlobalDefaultGameMode();
	const UClass* GameMode = FSoftClassPath(Path).TryLoadClass<AGameModeBase>();
	TestTrue(FString::Printf(TEXT("global default game mode '%s' is ALureGameMode or a child"), *Path), GameMode && GameMode->IsChildOf(ALureGameMode::StaticClass()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveConfigGameModeSpawnsLureCharacter, "Project.Movement.QA.Config.GameModeSpawnsLureCharacter", QAMovement::Flags)
bool FQAMoveConfigGameModeSpawnsLureCharacter::RunTest(const FString& Parameters)
{
	const UClass* Pawn = GetDefault<ALureGameMode>()->DefaultPawnClass;
	TestTrue(FString::Printf(TEXT("DefaultPawnClass %s is an ALurePlayerCharacter"), Pawn ? *Pawn->GetName() : TEXT("null")), Pawn && Pawn->IsChildOf(ALurePlayerCharacter::StaticClass()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQAMoveConfigBinaryTableMatchesCsv, "Project.Movement.QA.Config.BinaryTableMatchesCsv", QAMovement::Flags)
bool FQAMoveConfigBinaryTableMatchesCsv::RunTest(const FString& Parameters)
{
	const TSoftObjectPtr<UDataTable>& Soft = GetDefault<ULureCharacterSettings>()->MovementTable;
	const FString PackageName = Soft.ToSoftObjectPath().GetLongPackageName();
	if (PackageName.IsEmpty() || !FPackageName::DoesPackageExist(PackageName))
	{
		AddInfo(FString::Printf(TEXT("%s is not imported yet (lane or before the editor-operator import): nothing to compare."), *PackageName));
		return true;
	}
	const UDataTable* Asset = Soft.LoadSynchronous();
	UDataTable* Csv = QAM::ShippedTable(*this);
	if (!TestNotNull(TEXT("asset loads"), Asset) || !Csv)
	{
		return false;
	}
	TestTrue(TEXT("asset row struct is FLureMovementRow"), Asset->GetRowStruct() == FLureMovementRow::StaticStruct());
	TestEqual(TEXT("same row count"), Asset->GetRowNames().Num(), Csv->GetRowNames().Num());
	for (const FName& Name : Csv->GetRowNames())
	{
		const FLureMovementRow* A = Asset->FindRow<FLureMovementRow>(Name, TEXT("QA"), false);
		const FLureMovementRow* B = Csv->FindRow<FLureMovementRow>(Name, TEXT("QA"), false);
		TestTrue(FString::Printf(TEXT("row %s: asset == CSV (re-import data/tables/DT_Movement.csv if not)"), *Name.ToString()), A && B && QAM::RowsEqual(*A, *B));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

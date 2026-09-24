// Lure: catch handling types (T-030).

#include "Catch/LureCatchTypes.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

DEFINE_LOG_CATEGORY(LogLureCatch);

// ---------------------------------------------------------------------------------------------------------------------
// Freshness state
// ---------------------------------------------------------------------------------------------------------------------

namespace LureCatchTypesPrivate
{
	float SafeRate(float Rate)
	{
		return FMath::IsFinite(Rate) ? FMath::Max(0.0f, Rate) : 0.0f;
	}

	float SafeSeconds(float Seconds)
	{
		return FMath::IsFinite(Seconds) ? FMath::Max(0.0f, Seconds) : 0.0f;
	}
}

float FLureFreshnessState::GetExposure(double Now) const
{
	using namespace LureCatchTypesPrivate;
	const double Elapsed = FMath::IsFinite(Now) && FMath::IsFinite(AnchorTime) ? FMath::Max(0.0, Now - AnchorTime) : 0.0;
	const double Exposure = static_cast<double>(SafeSeconds(ExposedSeconds)) + static_cast<double>(SafeRate(Rate)) * Elapsed;
	return static_cast<float>(FMath::Min(Exposure, static_cast<double>(TNumericLimits<float>::Max())));
}

void FLureFreshnessState::SetRate(float NewRate, double Now)
{
	ExposedSeconds = GetExposure(Now);
	AnchorTime = FMath::IsFinite(Now) ? Now : AnchorTime;
	Rate = LureCatchTypesPrivate::SafeRate(NewRate);
}

FLureFreshnessState FLureFreshnessState::StartAt(double Now, float InRate)
{
	FLureFreshnessState State;
	State.ExposedSeconds = 0.0f;
	State.AnchorTime = FMath::IsFinite(Now) ? Now : 0.0;
	State.Rate = LureCatchTypesPrivate::SafeRate(InRate);
	return State;
}

FLureCaughtFish FLureCaughtFish::Landed(const FFishInstance& InFish, double Now)
{
	FLureCaughtFish Caught;
	Caught.Fish = InFish;
	Caught.Freshness = FLureFreshnessState::StartAt(Now, 1.0f);
	return Caught;
}

// ---------------------------------------------------------------------------------------------------------------------
// Rows
// ---------------------------------------------------------------------------------------------------------------------

bool FLureFreshnessRow::Validate(FString& OutProblem) const
{
	if (!FMath::IsFinite(GraceSeconds) || GraceSeconds < 0.0f)
	{
		OutProblem = FString::Printf(TEXT("GraceSeconds %g must be >= 0"), GraceSeconds);
		return false;
	}
	if (!FMath::IsFinite(SpoilSeconds) || !(SpoilSeconds > 0.0f))
	{
		OutProblem = FString::Printf(TEXT("SpoilSeconds %g must be > 0"), SpoilSeconds);
		return false;
	}
	if (!FMath::IsFinite(CurveExponent) || !(CurveExponent > 0.0f))
	{
		OutProblem = FString::Printf(TEXT("CurveExponent %g must be > 0"), CurveExponent);
		return false;
	}
	if (!FMath::IsFinite(MinValueShare) || MinValueShare < 0.0f || MinValueShare > 1.0f)
	{
		OutProblem = FString::Printf(TEXT("MinValueShare %g must be in [0, 1]"), MinValueShare);
		return false;
	}
	return true;
}

FLureFreshnessRow FLureFreshnessRow::GetFallbackRow()
{
	FLureFreshnessRow Row; // the member defaults are the shipped Default row (a data test keeps them equal)
	Row.DevComment = TEXT("Built-in fallback (DT_Freshness or its row is missing)");
	return Row;
}

bool FLureCatchRow::Validate(FString& OutProblem) const
{
	struct FCheck
	{
		const TCHAR* Name;
		float Value;
		float Min;
		float Max;
		bool bExclusiveMin;
	};
	const FCheck Checks[] = {
		{ TEXT("HangLineLength"), HangLineLength, 0.0f, 1000.0f, true },
		{ TEXT("HangDamping"), HangDamping, 0.0f, 100.0f, false },
		{ TEXT("ReachDistance"), ReachDistance, 0.0f, 2000.0f, true },
		{ TEXT("FocusAngleDeg"), FocusAngleDeg, 0.0f, 90.0f, true },
		{ TEXT("DropForward"), DropForward, 0.0f, 1000.0f, false },
		{ TEXT("DropArcTime"), DropArcTime, 0.0f, 10.0f, false },
		{ TEXT("PutDownDistance"), PutDownDistance, 0.0f, 1000.0f, true },
		{ TEXT("PutDownMaxFall"), PutDownMaxFall, 0.0f, 100000.0f, true },
		{ TEXT("LidOpenPitch"), LidOpenPitch, 0.0f, 180.0f, false },
		{ TEXT("LidOpenTime"), LidOpenTime, 0.0f, 10.0f, false },
	};
	for (const FCheck& Check : Checks)
	{
		const bool bLowOk = Check.bExclusiveMin ? Check.Value > Check.Min : Check.Value >= Check.Min;
		if (!FMath::IsFinite(Check.Value) || !bLowOk || Check.Value > Check.Max)
		{
			OutProblem = FString::Printf(TEXT("%s %g must be in %s%g, %g]"), Check.Name, Check.Value, Check.bExclusiveMin ? TEXT("(") : TEXT("["), Check.Min, Check.Max);
			return false;
		}
	}
	return true;
}

FLureCatchRow FLureCatchRow::GetFallbackRow()
{
	FLureCatchRow Row; // the member defaults are the shipped Default row (a data test keeps them equal)
	Row.DevComment = TEXT("Built-in fallback (DT_Catch or its row is missing)");
	return Row;
}

bool FLureCoolerDisplayRow::Validate(FString& OutProblem) const
{
	if (!FMath::IsFinite(MaxFishScale) || !(MaxFishScale > 0.0f) || MaxFishScale > 10.0f)
	{
		OutProblem = FString::Printf(TEXT("MaxFishScale %g must be in (0, 10]"), MaxFishScale);
		return false;
	}
	if (!FMath::IsFinite(PoseTime) || PoseTime < 0.0f)
	{
		OutProblem = FString::Printf(TEXT("PoseTime %g must be >= 0"), PoseTime);
		return false;
	}
	if (!FMath::IsFinite(LieOffsetCm) || LieOffsetCm < 0.0f || LieOffsetCm > 50.0f)
	{
		OutProblem = FString::Printf(TEXT("LieOffsetCm %g must be in [0, 50]"), LieOffsetCm);
		return false;
	}
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (Slots[Index].Location.ContainsNaN() || Slots[Index].Rotation.ContainsNaN() || Slots[Index].Location.GetAbsMax() > 500.0)
		{
			OutProblem = FString::Printf(TEXT("slot %d is not a sane transform (NaN, or farther than 5 m from the Contents socket)"), Index);
			return false;
		}
	}
	return true;
}

FLureCoolerDisplayRow FLureCoolerDisplayRow::GetFallbackRow()
{
	// The shipped Starter row (a data test keeps them equal): the slot table of SK_Fish.anim.md "Cooler display"
	// (art/recipes/anim_fish_cooler.py), curled fish on alternating sides, bottom of the pile first, Z = the bed.
	struct FSlotData
	{
		float X, Y, BedZ, Yaw, Roll;
	};
	static const FSlotData Table[] = { { 7.5f, -2.5f, 0.0f, 95.0f, 90.0f }, { 5.5f, -2.0f, 6.28f, -125.0f, -90.0f },
		{ 6.5f, 1.0f, 12.35f, 140.0f, 90.0f }, { 10.0f, 2.5f, 18.72f, -100.0f, -90.0f } };
	FLureCoolerDisplayRow Row;
	for (const FSlotData& Data : Table)
	{
		FLureCoolerDisplaySlot Slot;
		Slot.Location = FVector(Data.X, Data.Y, Data.BedZ);
		Slot.Rotation = FRotator(0.0f, Data.Yaw, Data.Roll);
		Row.Slots.Add(Slot);
	}
	Row.FishPose = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(TEXT("/Game/Art/Fish/A_Fish_Curled.A_Fish_Curled")));
	Row.PoseTime = 0.0f;
	Row.MaxFishScale = 1.0f;
	Row.LieOffsetCm = 4.25f;
	Row.DevComment = TEXT("Built-in display layout = the shipped Starter row (DT_CoolerDisplay or the cooler's row is missing)");
	return Row;
}

// ---------------------------------------------------------------------------------------------------------------------
// Freshness rules
// ---------------------------------------------------------------------------------------------------------------------

float FLureFreshness::GetFreshness01(const FLureFreshnessRow& Row, float ExposureSeconds)
{
	if (!FMath::IsFinite(ExposureSeconds))
	{
		return ExposureSeconds > 0.0f ? 0.0f : 1.0f; // +inf = spoiled, NaN or -inf = fresh
	}
	const float Grace = FMath::IsFinite(Row.GraceSeconds) ? FMath::Max(0.0f, Row.GraceSeconds) : 0.0f;
	const float Spoil = (FMath::IsFinite(Row.SpoilSeconds) && Row.SpoilSeconds > 0.0f) ? Row.SpoilSeconds : 1.0f;
	const float Exponent = (FMath::IsFinite(Row.CurveExponent) && Row.CurveExponent > 0.0f) ? Row.CurveExponent : 1.0f;
	const float X = FMath::Clamp((ExposureSeconds - Grace) / Spoil, 0.0f, 1.0f);
	return FMath::Clamp(1.0f - FMath::Pow(X, Exponent), 0.0f, 1.0f);
}

float FLureFreshness::GetValueShare(const FLureFreshnessRow& Row, float ExposureSeconds)
{
	const float Min = FMath::IsFinite(Row.MinValueShare) ? FMath::Clamp(Row.MinValueShare, 0.0f, 1.0f) : 0.0f;
	return FMath::Clamp(Min + (1.0f - Min) * GetFreshness01(Row, ExposureSeconds), Min, 1.0f);
}

int32 FLureFreshness::GetCurrentValue(const FFishInstance& Fish, float ValueShare)
{
	return GetSellPrice(Fish, ValueShare, 1.0f);
}

int32 FLureFreshness::GetSellPrice(const FFishInstance& Fish, float ValueShare, float SellMultiplier)
{
	const float Share = FMath::IsFinite(ValueShare) ? FMath::Clamp(ValueShare, 0.0f, 1.0f) : 1.0f;
	if (!FMath::IsFinite(SellMultiplier) || !(SellMultiplier > 0.0f))
	{
		return 0;
	}
	if (!(Share > 0.0f))
	{
		// A share of 0 (MinValueShare 0, fully spoiled) still pays the minimum for a real fish.
		return (Fish.IsValid() && Fish.Value > 0) ? 1 : 0;
	}
	return FLureProgressionRules::GetSellPrice(Fish, Share * SellMultiplier);
}

FLureFreshnessRow FLureFreshness::FindRow(const UDataTable* Table, FName SpeciesId, FName DefaultRowName, bool* bOutFallback)
{
	if (bOutFallback)
	{
		*bOutFallback = false;
	}
	if (Table && Table->GetRowStruct() && Table->GetRowStruct()->IsChildOf(FLureFreshnessRow::StaticStruct()))
	{
		for (const FName& Name : { SpeciesId, DefaultRowName })
		{
			if (Name.IsNone())
			{
				continue;
			}
			if (const FLureFreshnessRow* Row = reinterpret_cast<const FLureFreshnessRow*>(Table->FindRowUnchecked(Name)))
			{
				FString Problem;
				if (Row->Validate(Problem))
				{
					return *Row;
				}
			}
		}
	}
	if (bOutFallback)
	{
		*bOutFallback = true;
	}
	return FLureFreshnessRow::GetFallbackRow();
}

double FLureFreshness::GetServerTime(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return 0.0;
	}
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

// ---------------------------------------------------------------------------------------------------------------------
// Pendulum
// ---------------------------------------------------------------------------------------------------------------------

void FLureHangPendulum::Reset(const FVector& Pivot, float Length)
{
	const float SafeLength = (FMath::IsFinite(Length) && Length > 0.0f) ? Length : 1.0f;
	Bob = Pivot - FVector::UpVector * SafeLength;
	Velocity = FVector::ZeroVector;
	bInitialized = true;
}

void FLureHangPendulum::Step(const FVector& Pivot, float Length, float GravityZ, float Damping, float DeltaSeconds)
{
	const float SafeLength = (FMath::IsFinite(Length) && Length > 0.0f) ? Length : 1.0f;
	if (!bInitialized || Pivot.ContainsNaN() || Bob.ContainsNaN() || Velocity.ContainsNaN()
		|| FVector::DistSquared(Pivot, Bob) > FMath::Square(4.0 * SafeLength))
	{
		Reset(Pivot.ContainsNaN() ? FVector::ZeroVector : Pivot, SafeLength);
		return;
	}
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f)
	{
		return;
	}
	if (FMath::Abs(FVector::Dist(Pivot, Bob) - SafeLength) > 0.5 * SafeLength)
	{
		// A big jump (a swing that starts where a landed fish was, a hitch): onto the line's reach in its direction, with no
		// kick. Smaller offsets (the player walking, the rod bobbing) go through the constraint below and swing the fish.
		FVector Direction = (Bob - Pivot).GetSafeNormal();
		Bob = Pivot + (Direction.IsNearlyZero() ? -FVector::UpVector : Direction) * SafeLength;
	}
	const float Gravity = FMath::IsFinite(GravityZ) ? GravityZ : -980.0f;
	const float SafeDamping = FMath::IsFinite(Damping) ? FMath::Max(0.0f, Damping) : 0.0f;
	constexpr float MaxStep = 1.0f / 60.0f;
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaSeconds / MaxStep), 1, 8);
	const float Dt = FMath::Min(DeltaSeconds, 8.0f * MaxStep) / static_cast<float>(Steps);
	for (int32 Index = 0; Index < Steps; ++Index)
	{
		Velocity *= FMath::Exp(-SafeDamping * Dt);
		Velocity.Z += Gravity * Dt;
		const FVector Predicted = Bob + Velocity * Dt;
		FVector Direction = (Predicted - Pivot).GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			Direction = -FVector::UpVector;
		}
		const FVector Constrained = Pivot + Direction * SafeLength;
		Velocity = (Constrained - Bob) / Dt;
		Bob = Constrained;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------------------------------------------------

namespace LureCatchTypesPrivate
{
	template <typename RowType>
	TArray<FString> ValidateRows(const UDataTable* Table, FName RequiredRow, const TCHAR* TableName)
	{
		TArray<FString> Problems;
		if (!Table)
		{
			Problems.Add(FString::Printf(TEXT("%s: no table"), TableName));
			return Problems;
		}
		if (Table->GetRowStruct() != RowType::StaticStruct())
		{
			Problems.Add(FString::Printf(TEXT("%s: row struct is %s, expected %s"), TableName, *GetNameSafe(Table->GetRowStruct()), *RowType::StaticStruct()->GetName()));
			return Problems;
		}
		if (Table->GetRowMap().Num() == 0)
		{
			Problems.Add(FString::Printf(TEXT("%s: the table has no rows"), TableName));
			return Problems;
		}
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			FString Problem;
			if (!reinterpret_cast<const RowType*>(Pair.Value)->Validate(Problem))
			{
				Problems.Add(FString::Printf(TEXT("%s row %s: %s"), TableName, *Pair.Key.ToString(), *Problem));
			}
		}
		if (!RequiredRow.IsNone() && !Table->GetRowMap().Contains(RequiredRow))
		{
			Problems.Add(FString::Printf(TEXT("%s: the row '%s' the settings name is missing"), TableName, *RequiredRow.ToString()));
		}
		return Problems;
	}
}

TArray<FString> FLureCatchData::ValidateFreshnessTable(const UDataTable* Table, FName DefaultRow)
{
	return LureCatchTypesPrivate::ValidateRows<FLureFreshnessRow>(Table, DefaultRow, TEXT("DT_Freshness"));
}

TArray<FString> FLureCatchData::ValidateCatchTable(const UDataTable* Table, FName Row)
{
	return LureCatchTypesPrivate::ValidateRows<FLureCatchRow>(Table, Row, TEXT("DT_Catch"));
}

TArray<FString> FLureCatchData::ValidateCoolerDisplayTable(const UDataTable* Table, const UDataTable* CoolerTable)
{
	TArray<FString> Problems = LureCatchTypesPrivate::ValidateRows<FLureCoolerDisplayRow>(Table, NAME_None, TEXT("DT_CoolerDisplay"));
	if (Problems.Num() == 0 && Table && CoolerTable)
	{
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			if (!CoolerTable->GetRowMap().Contains(Pair.Key))
			{
				Problems.Add(FString::Printf(TEXT("DT_CoolerDisplay row %s: DT_Cooler has no row of that name (display rows are named like the cooler type they dress)"), *Pair.Key.ToString()));
			}
		}
	}
	return Problems;
}

// Lure: reel fight, line tension and gear data (T-007).

#include "Fishing/FishFightTypes.h"
#include "Engine/DataTable.h"

namespace LureFightTypesPrivate
{
	bool AllFiniteNonNegative(std::initializer_list<float> Values)
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

	const TCHAR* SlotName(ELureGearSlot Slot)
	{
		switch (Slot)
		{
		case ELureGearSlot::Rod: return TEXT("Rod");
		case ELureGearSlot::Line: return TEXT("Line");
		case ELureGearSlot::Hook: return TEXT("Hook");
		default: return TEXT("?");
		}
	}

	FLureFightMove MakeMove(const TCHAR* Id, const TCHAR* Label, float Weight, float AggressionWeight, float DurationMin, float DurationMax,
		float Pull, float Speed, float Away, float Side, bool bRandomSide, bool bRest)
	{
		FLureFightMove Move;
		Move.Id = FName(Id);
		Move.Label = FText::FromString(Label);
		Move.Weight = Weight;
		Move.AggressionWeight = AggressionWeight;
		Move.DurationMin = DurationMin;
		Move.DurationMax = DurationMax;
		Move.Pull = Pull;
		Move.Speed = Speed;
		Move.Away = Away;
		Move.Side = Side;
		Move.RandomSide = bRandomSide;
		Move.Rest = bRest;
		return Move;
	}
}

// ---- Gear ----

bool FLureGearRow::Validate(FString& OutProblem) const
{
	using namespace LureFightTypesPrivate;
	if (Price < 0)
	{
		OutProblem = TEXT("Price must be >= 0");
		return false;
	}
	switch (Slot)
	{
	case ELureGearSlot::Rod:
		if (!AllFiniteNonNegative({ RodPower, ReelSpeed, Drag, CastDistanceMultiplier }) || RodPower <= 0.f || ReelSpeed <= 0.f || CastDistanceMultiplier <= 0.f)
		{
			OutProblem = TEXT("a rod needs RodPower, ReelSpeed and CastDistanceMultiplier > 0 and Drag >= 0");
			return false;
		}
		return true;
	case ELureGearSlot::Line:
		if (!AllFiniteNonNegative({ LineStrength, SpoolLength }) || LineStrength <= 0.f || SpoolLength <= 0.f)
		{
			OutProblem = TEXT("a line needs LineStrength and SpoolLength > 0");
			return false;
		}
		return true;
	case ELureGearSlot::Hook:
		if (!AllFiniteNonNegative({ HookSecurity, Luck }) || HookSecurity <= 0.f)
		{
			OutProblem = TEXT("a hook needs HookSecurity > 0 and Luck >= 0");
			return false;
		}
		return true;
	default:
		OutProblem = TEXT("unknown Slot");
		return false;
	}
}

FName FLureGearLoadout::Get(ELureGearSlot Slot) const
{
	switch (Slot)
	{
	case ELureGearSlot::Rod: return Rod;
	case ELureGearSlot::Line: return Line;
	case ELureGearSlot::Hook: return Hook;
	default: return NAME_None;
	}
}

void FLureGearLoadout::Set(ELureGearSlot Slot, FName Id)
{
	switch (Slot)
	{
	case ELureGearSlot::Rod: Rod = Id; break;
	case ELureGearSlot::Line: Line = Id; break;
	case ELureGearSlot::Hook: Hook = Id; break;
	default: break;
	}
}

const FLureGearRow* FLureGear::FindItem(const UDataTable* Table, FName Id)
{
	if (!Table || Id.IsNone() || !Table->GetRowStruct() || !Table->GetRowStruct()->IsChildOf(FLureGearRow::StaticStruct()))
	{
		return nullptr;
	}
	return reinterpret_cast<const FLureGearRow*>(Table->FindRowUnchecked(Id));
}

FLureGearRow FLureGear::GetFallbackItem(ELureGearSlot Slot)
{
	// The struct defaults are the starter items (Rod_Starter, Line_Mono, Hook_Shrimp); only the slot's columns count.
	FLureGearRow Row;
	Row.Slot = Slot;
	Row.DisplayName = FText::FromString(FString::Printf(TEXT("Built-in %s"), LureFightTypesPrivate::SlotName(Slot)));
	if (Slot == ELureGearSlot::Hook)
	{
		Row.BaitTag = FGameplayTag::RequestGameplayTag(TEXT("Bait.Shrimp"), /*ErrorIfNotFound*/ false);
	}
	return Row;
}

void FLureGear::ApplyItem(FLureGearStats& Stats, const FLureGearRow& Row, FName Id)
{
	switch (Row.Slot)
	{
	case ELureGearSlot::Rod:
		Stats.RodId = Id;
		Stats.RodPower = Row.RodPower;
		Stats.ReelSpeed = Row.ReelSpeed;
		Stats.Drag = Row.Drag;
		Stats.CastDistanceMultiplier = Row.CastDistanceMultiplier;
		break;
	case ELureGearSlot::Line:
		Stats.LineId = Id;
		Stats.LineStrength = Row.LineStrength;
		Stats.SpoolLength = Row.SpoolLength;
		break;
	case ELureGearSlot::Hook:
		Stats.HookId = Id;
		Stats.HookSecurity = Row.HookSecurity;
		Stats.BaitTag = Row.BaitTag;
		Stats.Luck = Row.Luck;
		break;
	default:
		break;
	}
}

void FLureGear::ApplyDragLineCap(FLureGearStats& Stats, float DragLineCap)
{
	if (!FMath::IsFinite(DragLineCap) || DragLineCap <= 0.f || !FMath::IsFinite(Stats.LineStrength) || Stats.LineStrength <= 0.f)
	{
		return;
	}
	Stats.Drag = FMath::Min(Stats.Drag, Stats.LineStrength * FMath::Min(DragLineCap, 1.f));
}

bool FLureGear::CanEquip(const UDataTable* Table, ELureGearSlot Slot, FName Id, FString* OutProblem)
{
	auto Fail = [OutProblem](const FString& Problem)
	{
		if (OutProblem)
		{
			*OutProblem = Problem;
		}
		return false;
	};
	const FLureGearRow* Row = FindItem(Table, Id);
	if (!Row)
	{
		return Fail(FString::Printf(TEXT("no DT_Gear row '%s'"), *Id.ToString()));
	}
	if (Row->Slot != Slot)
	{
		return Fail(FString::Printf(TEXT("'%s' is a %s, not a %s"), *Id.ToString(), LureFightTypesPrivate::SlotName(Row->Slot), LureFightTypesPrivate::SlotName(Slot)));
	}
	FString Problem;
	if (!Row->Validate(Problem))
	{
		return Fail(FString::Printf(TEXT("'%s' is invalid (%s)"), *Id.ToString(), *Problem));
	}
	return true;
}

FLureGearStats FLureGear::Resolve(const UDataTable* Table, const FLureGearLoadout& Loadout, TArray<FString>* OutProblems)
{
	FLureGearStats Stats;
	for (const ELureGearSlot Slot : { ELureGearSlot::Rod, ELureGearSlot::Line, ELureGearSlot::Hook })
	{
		const FName Id = Loadout.Get(Slot);
		FString Problem;
		if (CanEquip(Table, Slot, Id, &Problem))
		{
			ApplyItem(Stats, *FindItem(Table, Id), Id);
			continue;
		}
		ApplyItem(Stats, GetFallbackItem(Slot), NAME_None);
		Stats.bUsedFallback = true;
		if (OutProblems)
		{
			OutProblems->Add(FString::Printf(TEXT("%s: %s"), LureFightTypesPrivate::SlotName(Slot), Table ? *Problem : TEXT("no gear table")));
		}
	}
	return Stats;
}

// ---- Fight patterns ----

int32 FLureFightPatternRow::FindMove(FName MoveId) const
{
	if (MoveId.IsNone())
	{
		return INDEX_NONE;
	}
	return Moves.IndexOfByPredicate([MoveId](const FLureFightMove& Move) { return Move.Id == MoveId; });
}

bool FLureFightPatternRow::Validate(FString& OutProblem) const
{
	if (Moves.Num() == 0)
	{
		OutProblem = TEXT("no moves");
		return false;
	}
	TSet<FName> Ids;
	bool bAnyWeight = false;
	for (const FLureFightMove& Move : Moves)
	{
		const FString Name = Move.Id.ToString();
		if (Move.Id.IsNone() || Ids.Contains(Move.Id))
		{
			OutProblem = FString::Printf(TEXT("move id '%s' is empty or used twice"), *Name);
			return false;
		}
		Ids.Add(Move.Id);
		if (!LureFightTypesPrivate::AllFiniteNonNegative({ Move.Weight, Move.AggressionWeight, Move.DurationMin, Move.DurationMax, Move.Pull, Move.Speed }))
		{
			OutProblem = FString::Printf(TEXT("move %s: Weight, AggressionWeight, durations, Pull and Speed must be finite and >= 0"), *Name);
			return false;
		}
		if (Move.DurationMin <= 0.f || Move.DurationMax < Move.DurationMin)
		{
			OutProblem = FString::Printf(TEXT("move %s: 0 < DurationMin <= DurationMax"), *Name);
			return false;
		}
		for (const float Share : { Move.Away, Move.Side, Move.Down })
		{
			if (!FMath::IsFinite(Share) || Share < -1.f || Share > 1.f)
			{
				OutProblem = FString::Printf(TEXT("move %s: Away, Side and Down must be in [-1, 1]"), *Name);
				return false;
			}
		}
		// A calm fish (aggression 0, the default of any species without an Aggression stat) picks by Weight alone.
		bAnyWeight |= Move.Weight > 0.f;
	}
	if (!bAnyWeight)
	{
		OutProblem = TEXT("no move can be picked by a calm fish (every move's Weight is 0; AggressionWeight alone is not enough)");
		return false;
	}
	if (!OpeningMove.IsNone() && FindMove(OpeningMove) == INDEX_NONE)
	{
		OutProblem = FString::Printf(TEXT("OpeningMove '%s' is not a move"), *OpeningMove.ToString());
		return false;
	}
	return true;
}

FLureFightPatternRow FLureFightPatternRow::GetFallbackPattern()
{
	using LureFightTypesPrivate::MakeMove;
	FLureFightPatternRow Pattern;
	Pattern.DisplayName = FText::FromString(TEXT("Built-in"));
	Pattern.OpeningMove = TEXT("Run");
	Pattern.Moves = {
		MakeMove(TEXT("Run"), TEXT("running!"), 2.f, 0.05f, 1.5f, 3.f, 1.5f, 1.5f, 1.f, 0.3f, true, false),
		MakeMove(TEXT("Swim"), TEXT("swimming"), 3.f, 0.f, 1.5f, 3.f, 1.f, 0.7f, 0.7f, 0.4f, true, false),
		MakeMove(TEXT("Rest"), TEXT("resting"), 2.f, 0.f, 1.f, 2.5f, 0.3f, 0.f, 0.f, 0.f, false, true),
	};
	return Pattern;
}

// ---- Fight tuning ----

bool FLureFishFightRow::Validate(FString& OutProblem) const
{
	if (!LureFightTypesPrivate::AllFiniteNonNegative({ PullPerStrength, SpeedPerStat, StaminaPerStat, RestDifficultyExponent, TiredPull, ExhaustedStamina,
		StaminaRecovery, ReelStrain, ReelLoad, DragHold, TensionRiseTime, TensionFallTime, SnapGraceTime, SlackShare, SlackGraceTime, LandDistance,
		MaxDepth, DepthRecovery, MaxSideDeg, DiveBobberShare, RodTensionPitchDeg, RodShakeDeg, TautTension }))
	{
		OutProblem = TEXT("every value must be a finite number >= 0");
		return false;
	}
	const FGameplayTag FishStatRoot = FGameplayTag::RequestGameplayTag(TEXT("Fish.Stat"), /*ErrorIfNotFound*/ false);
	auto IsFishStat = [&FishStatRoot](const FGameplayTag& Tag)
	{
		return Tag.IsValid() && FishStatRoot.IsValid() && Tag != FishStatRoot && Tag.MatchesTag(FishStatRoot);
	};
	if (!IsFishStat(StrengthStat) || !IsFishStat(StaminaStat) || !IsFishStat(SpeedStat) || !IsFishStat(AggressionStat))
	{
		OutProblem = TEXT("StrengthStat, StaminaStat, SpeedStat and AggressionStat must be registered Fish.Stat tags");
		return false;
	}
	if (PullPerStrength <= 0.f || StaminaPerStat <= 0.f)
	{
		OutProblem = TEXT("PullPerStrength and StaminaPerStat must be > 0");
		return false;
	}
	if (TiredPull > 1.f || ExhaustedStamina >= 1.f || DragHold >= 1.f || DiveBobberShare > 1.f)
	{
		OutProblem = TEXT("TiredPull and DiveBobberShare in [0, 1], ExhaustedStamina and DragHold in [0, 1)");
		return false;
	}
	if (!FMath::IsFinite(DragLineCap) || DragLineCap <= 0.f || DragLineCap > 1.f)
	{
		OutProblem = TEXT("DragLineCap must be in (0, 1]");
		return false;
	}
	if (SlackGraceTime <= 0.f || LandDistance <= 0.f || TautTension <= 0.f)
	{
		OutProblem = TEXT("SlackGraceTime, LandDistance and TautTension must be > 0");
		return false;
	}
	if (SimRate < 10 || SimRate > 240)
	{
		OutProblem = FString::Printf(TEXT("SimRate %d must be in [10, 240]"), SimRate);
		return false;
	}
	return ValidateRodSteering(OutProblem);
}

bool FLureFishFightRow::ValidateRodSteering(FString& OutProblem) const
{
	// T-028 columns (reel-fight-rules.md "Rod steering").
	if (!LureFightTypesPrivate::AllFiniteNonNegative({ RodAimUpDeg, RodAimDownDeg, RodAimSideDeg, PitchBackPressure, PitchDipPressure, PitchDipPower, SideMinShare,
		SideLeverage, SideTurnRate, SideTurnPull, SideDrain, ReelSpeedMin, ReelSpeedMax, ReelLoadPerSpeed, CameraFollowTime, CameraRodYawShare,
		CameraRodPitchShare, RodAimLookPitchDeg, RodAimLookYawDeg, RodAimBlendTime }))
	{
		OutProblem = TEXT("every rod-steering value must be a finite number >= 0");
		return false;
	}
	if (RodAimUpDeg < 1.f || RodAimDownDeg < 1.f || RodAimSideDeg < 1.f)
	{
		OutProblem = TEXT("RodAimUpDeg, RodAimDownDeg and RodAimSideDeg must be >= 1 degree");
		return false;
	}
	if (PitchDipPressure > 0.95f || PitchDipPower > 0.95f || SideLeverage > 0.95f || SideTurnPull > 0.95f)
	{
		OutProblem = TEXT("PitchDipPressure, PitchDipPower, SideLeverage and SideTurnPull must be <= 0.95 (the rod and the fish keep some power)");
		return false;
	}
	if (SideMinShare > 1.f || CameraRodYawShare > 1.f || CameraRodPitchShare > 1.f)
	{
		OutProblem = TEXT("SideMinShare, CameraRodYawShare and CameraRodPitchShare must be in [0, 1]");
		return false;
	}
	if (ReelSteps < 1 || ReelSteps > 9 || ReelDefaultStep < 1 || ReelDefaultStep > ReelSteps)
	{
		OutProblem = FString::Printf(TEXT("ReelSteps %d must be in [1, 9] and ReelDefaultStep %d in [1, ReelSteps]"), ReelSteps, ReelDefaultStep);
		return false;
	}
	if (ReelSpeedMin < 0.05f || ReelSpeedMax < ReelSpeedMin)
	{
		OutProblem = TEXT("ReelSpeedMin must be >= 0.05 and ReelSpeedMax >= ReelSpeedMin");
		return false;
	}
	// T-028b (O8): the default step is the plain T-007 reel (speed 1), so a player who never touches the wheel fights the T-007 fight.
	if (ReelSteps > 1)
	{
		const float DefaultSpeed = ReelSpeedMin + (ReelSpeedMax - ReelSpeedMin) * static_cast<float>(ReelDefaultStep - 1) / static_cast<float>(ReelSteps - 1);
		if (!FMath::IsNearlyEqual(DefaultSpeed, 1.f, 1.0e-4f))
		{
			OutProblem = FString::Printf(TEXT("the default reel step %d of %d has speed %.4f; it must be 1 (set ReelSpeedMin/ReelSpeedMax/ReelDefaultStep so it is)"),
				ReelDefaultStep, ReelSteps, DefaultSpeed);
			return false;
		}
	}
	return true;
}

FLureFishFightRow FLureFishFightRow::GetFallbackRow()
{
	// The struct defaults plus the four stat tags ARE the shipped DT_FishFight "Default" row (a test checks they match).
	FLureFishFightRow Row;
	Row.StrengthStat = FGameplayTag::RequestGameplayTag(TEXT("Fish.Stat.Strength"), /*ErrorIfNotFound*/ false);
	Row.StaminaStat = FGameplayTag::RequestGameplayTag(TEXT("Fish.Stat.Stamina"), /*ErrorIfNotFound*/ false);
	Row.SpeedStat = FGameplayTag::RequestGameplayTag(TEXT("Fish.Stat.Speed"), /*ErrorIfNotFound*/ false);
	Row.AggressionStat = FGameplayTag::RequestGameplayTag(TEXT("Fish.Stat.Aggression"), /*ErrorIfNotFound*/ false);
	return Row;
}

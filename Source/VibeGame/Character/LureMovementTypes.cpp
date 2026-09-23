// Lure: movement data types (T-004).

#include "Character/LureMovementTypes.h"

DEFINE_LOG_CATEGORY(LogLureMovement);

const TCHAR* FLureMovementData::FallbackWarningMarker = TEXT("using built-in fallback rows");

bool FLureMovementRow::Validate(FString& OutProblem) const
{
	const float Values[] = { MaxSpeed, MaxAcceleration, CapsuleHalfHeight, CapsuleRadius, EyeHeight, TransitionTime, NoiseMultiplier, JumpZVelocity,
		BobStepRate, BobVertical, BobLateral, BobRoll, BobPitch, BobYaw, BobForward, StanceDipPlayRate };
	for (const float Value : Values)
	{
		if (!FMath::IsFinite(Value))
		{
			OutProblem = TEXT("a value is not a finite number");
			return false;
		}
	}
	const float BobValues[] = { BobStepRate, BobVertical, BobLateral, BobRoll, BobPitch, BobYaw, BobForward, StanceDipPlayRate };
	for (const float Value : BobValues)
	{
		if (Value < 0.f)
		{
			OutProblem = TEXT("arms bob values and StanceDipPlayRate must be >= 0");
			return false;
		}
	}
	if (MaxSpeed <= 0.f)
	{
		OutProblem = FString::Printf(TEXT("MaxSpeed %.1f must be > 0"), MaxSpeed);
		return false;
	}
	if (MaxAcceleration <= 0.f)
	{
		OutProblem = FString::Printf(TEXT("MaxAcceleration %.1f must be > 0"), MaxAcceleration);
		return false;
	}
	if (CapsuleRadius <= 0.f)
	{
		OutProblem = FString::Printf(TEXT("CapsuleRadius %.1f must be > 0"), CapsuleRadius);
		return false;
	}
	if (CapsuleHalfHeight < CapsuleRadius)
	{
		OutProblem = FString::Printf(TEXT("CapsuleHalfHeight %.1f must be >= CapsuleRadius %.1f"), CapsuleHalfHeight, CapsuleRadius);
		return false;
	}
	if (EyeHeight <= 0.f || EyeHeight > 2.f * CapsuleHalfHeight)
	{
		OutProblem = FString::Printf(TEXT("EyeHeight %.1f must be inside the capsule (0, %.1f]"), EyeHeight, 2.f * CapsuleHalfHeight);
		return false;
	}
	if (TransitionTime < 0.f)
	{
		OutProblem = FString::Printf(TEXT("TransitionTime %.2f must be >= 0"), TransitionTime);
		return false;
	}
	if (NoiseMultiplier < 0.f)
	{
		OutProblem = FString::Printf(TEXT("NoiseMultiplier %.2f must be >= 0"), NoiseMultiplier);
		return false;
	}
	if (JumpZVelocity < 0.f || (CanJump && JumpZVelocity <= 0.f))
	{
		OutProblem = FString::Printf(TEXT("JumpZVelocity %.1f must be >= 0 (and > 0 when CanJump)"), JumpZVelocity);
		return false;
	}
	// Rod pose (T-006).
	const float RodValues[] = { RodMoveSpeedIn, RodMoveSpeedOut, RodStillDelay, RodPoseBlendTime, ArmsPitchFollowUp, RodHoldClearance };
	for (const float Value : RodValues)
	{
		if (!FMath::IsFinite(Value) || Value < 0.f)
		{
			OutProblem = TEXT("rod pose values must be finite and >= 0");
			return false;
		}
	}
	if (RodMoveSpeedOut > RodMoveSpeedIn)
	{
		OutProblem = FString::Printf(TEXT("RodMoveSpeedOut %.1f must be <= RodMoveSpeedIn %.1f"), RodMoveSpeedOut, RodMoveSpeedIn);
		return false;
	}
	if (ArmsPitchFollowUp > 1.f)
	{
		OutProblem = FString::Printf(TEXT("ArmsPitchFollowUp %.2f must be in [0, 1]"), ArmsPitchFollowUp);
		return false;
	}
	return true;
}

FName FLureMovementData::GetRowName(ELureMovementState State)
{
	static const FName Names[NumStates] = { TEXT("Stand"), TEXT("Sprint"), TEXT("Crouch"), TEXT("Prone") };
	const int32 Index = static_cast<int32>(State);
	return Names[FMath::Clamp(Index, 0, NumStates - 1)];
}

FLureMovementRow FLureMovementData::GetFallbackRow(ELureMovementState State)
{
	// Keep in step with data/tables/DT_Movement.csv (the shipped tuning). Used only when the asset or a row is missing/invalid.
	auto MakeRow = [](float MaxSpeed, float MaxAcceleration, float HalfHeight, float Radius, float EyeHeight, float TransitionTime, float Noise, float JumpZ, bool bCanJump)
	{
		FLureMovementRow Row;
		Row.MaxSpeed = MaxSpeed;
		Row.MaxAcceleration = MaxAcceleration;
		Row.CapsuleHalfHeight = HalfHeight;
		Row.CapsuleRadius = Radius;
		Row.EyeHeight = EyeHeight;
		Row.TransitionTime = TransitionTime;
		Row.NoiseMultiplier = Noise;
		Row.JumpZVelocity = JumpZ;
		Row.CanJump = bCanJump;
		return Row;
	};

	// Arms bob per SK_FPArms.anim.md (BobStepRate 0 = derived from MaxSpeed).
	auto WithBob = [](FLureMovementRow Row, float Vertical, float Lateral, float Roll, float Pitch, float Yaw, float Forward, float DipPlayRate)
	{
		Row.BobStepRate = 0.f;
		Row.BobVertical = Vertical;
		Row.BobLateral = Lateral;
		Row.BobRoll = Roll;
		Row.BobPitch = Pitch;
		Row.BobYaw = Yaw;
		Row.BobForward = Forward;
		Row.StanceDipPlayRate = DipPlayRate;
		return Row;
	};

	// Rod pose and fishing (T-006): the struct defaults (HoldRod, 15/5 cm/s, 0.3 s, follow-up 1, no clearance check, CanFish)
	// except Prone (prone hold / tuck, arms stay down when looking up, 130 cm wall check) and Sprint (no fishing).
	FLureMovementRow Row;
	switch (State)
	{
	case ELureMovementState::Sprint:
		Row = WithBob(MakeRow(600.f, 2048.f, 90.f, 34.f, 165.f, 0.25f, 2.5f, 440.f, true), 1.6f, 1.0f, 1.2f, 0.9f, 0.f, 0.3f, 1.0f);
		Row.CanFish = false;
		return Row;
	case ELureMovementState::Crouch:
		return WithBob(MakeRow(180.f, 1600.f, 55.f, 34.f, 95.f, 0.20f, 0.5f, 380.f, true), 0.5f, 0.9f, 0.9f, 0.3f, 0.f, 0.f, 1.0f);
	case ELureMovementState::Prone:
		Row = WithBob(MakeRow(90.f, 1200.f, 26.f, 25.f, 35.f, 0.45f, 0.2f, 0.f, false), 0.4f, 1.6f, 2.0f, 0.3f, 1.5f, 1.2f, 0.85f);
		Row.RodPoseStill = EFPArmsPose::ProneHold;
		Row.RodPoseMoving = EFPArmsPose::ProneTuck;
		Row.ArmsPitchFollowUp = 0.f;
		Row.RodHoldClearance = 130.f;
		return Row;
	case ELureMovementState::Stand:
	default:
		return WithBob(MakeRow(350.f, 2048.f, 90.f, 34.f, 165.f, 0.25f, 1.0f, 420.f, true), 0.8f, 0.6f, 0.6f, 0.4f, 0.f, 0.f, 1.0f);
	}
}

ELureMovementState FLureMovementData::ToMovementState(ELureStance Stance)
{
	switch (Stance)
	{
	case ELureStance::Crouch:
		return ELureMovementState::Crouch;
	case ELureStance::Prone:
		return ELureMovementState::Prone;
	case ELureStance::Stand:
	default:
		return ELureMovementState::Stand;
	}
}

uint8 FLureMovementData::ResolveRows(const UDataTable* Table, TArray<FLureMovementRow>& OutRows, TArray<FString>& OutProblems)
{
	OutRows.SetNum(NumStates);
	for (ELureMovementState State : TEnumRange<ELureMovementState>())
	{
		OutRows[static_cast<int32>(State)] = GetFallbackRow(State);
	}

	if (!Table)
	{
		OutProblems.Add(TEXT("no movement table"));
		return AllStatesMask;
	}

	const UScriptStruct* RowStruct = Table->GetRowStruct();
	if (!RowStruct || !RowStruct->IsChildOf(FLureMovementRow::StaticStruct()))
	{
		OutProblems.Add(FString::Printf(TEXT("row struct is '%s', expected 'LureMovementRow'"), RowStruct ? *RowStruct->GetName() : TEXT("none")));
		return AllStatesMask;
	}

	uint8 FallbackMask = 0;
	for (ELureMovementState State : TEnumRange<ELureMovementState>())
	{
		const int32 Index = static_cast<int32>(State);
		const FName RowName = GetRowName(State);
		// FindRowUnchecked: the struct was checked above, and it never logs (a missing row is reported below as one warning).
		const FLureMovementRow* Row = reinterpret_cast<const FLureMovementRow*>(Table->FindRowUnchecked(RowName));
		if (!Row)
		{
			OutProblems.Add(FString::Printf(TEXT("row '%s' is missing"), *RowName.ToString()));
			FallbackMask |= (1u << Index);
			continue;
		}

		FString Problem;
		if (!Row->Validate(Problem))
		{
			OutProblems.Add(FString::Printf(TEXT("row '%s' is invalid (%s)"), *RowName.ToString(), *Problem));
			FallbackMask |= (1u << Index);
			continue;
		}

		OutRows[Index] = *Row;
	}

	for (const FName& RowName : Table->GetRowNames())
	{
		bool bKnown = false;
		for (ELureMovementState State : TEnumRange<ELureMovementState>())
		{
			bKnown |= (RowName == GetRowName(State));
		}
		if (!bKnown)
		{
			OutProblems.Add(FString::Printf(TEXT("unknown row '%s' ignored"), *RowName.ToString()));
		}
	}

	return FallbackMask;
}

FString FLureMovementData::FormatResolveWarning(const FString& TableName, const TArray<FString>& Problems, uint8 FallbackMask)
{
	FString Message = FString::Printf(TEXT("Movement table %s: %s."), *TableName, *FString::Join(Problems, TEXT("; ")));
	if (FallbackMask != 0)
	{
		TArray<FString> States;
		for (ELureMovementState State : TEnumRange<ELureMovementState>())
		{
			if (FallbackMask & (1u << static_cast<int32>(State)))
			{
				States.Add(GetRowName(State).ToString());
			}
		}
		Message += FString::Printf(TEXT(" Movement is %s for: %s."), FallbackWarningMarker, *FString::Join(States, TEXT(", ")));
	}
	return Message;
}

// Lure: movement data types (T-004; swim rows T-026).

#include "Character/LureMovementTypes.h"

DEFINE_LOG_CATEGORY(LogLureMovement);

const TCHAR* FLureMovementData::FallbackWarningMarker = TEXT("using built-in fallback rows");

bool FLureMovementRow::Validate(FString& OutProblem) const
{
	const float Values[] = { MaxSpeed, MaxAcceleration, CapsuleHalfHeight, CapsuleRadius, EyeHeight, TransitionTime, NoiseMultiplier, JumpZVelocity,
		BobStepRate, BobVertical, BobLateral, BobRoll, BobPitch, BobYaw, BobForward, StanceDipPlayRate,
		ClimbMaxHeight, ClimbSpeed, SurfaceFloatDepth, ArmsPullBack, ExitTransitionTime,
		ClimbOutLowestTop, ClimbOutSurfaceTolerance, ClimbOutReach, SurfaceFloatSettleTime, SwimBrakingDeceleration };
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
	if (ClimbMaxHeight < 0.f || ClimbSpeed < 0.f || SurfaceFloatDepth < 0.f || ArmsPullBack < 0.f || ExitTransitionTime < 0.f)
	{
		OutProblem = TEXT("ClimbMaxHeight, ClimbSpeed, SurfaceFloatDepth, ArmsPullBack and ExitTransitionTime must be >= 0");
		return false;
	}
	if (ClimbMaxHeight > 0.f && ClimbSpeed <= 0.f)
	{
		OutProblem = FString::Printf(TEXT("ClimbSpeed must be > 0 when ClimbMaxHeight (%.1f) is set"), ClimbMaxHeight);
		return false;
	}
	if (ClimbOutLowestTop > 0.f)
	{
		OutProblem = FString::Printf(TEXT("ClimbOutLowestTop %.1f must be <= 0 (at or below the water surface)"), ClimbOutLowestTop);
		return false;
	}
	if (ClimbOutReach < 0.f || SwimBrakingDeceleration < 0.f || ClimbOutSurfaceTolerance < 0.f)
	{
		OutProblem = TEXT("ClimbOutReach, ClimbOutSurfaceTolerance and SwimBrakingDeceleration must be >= 0");
		return false;
	}
	if (SurfaceFloatSettleTime < 0.05f)
	{
		OutProblem = FString::Printf(TEXT("SurfaceFloatSettleTime %.2f must be >= 0.05 s"), SurfaceFloatSettleTime);
		return false;
	}
	return true;
}

FName FLureMovementData::GetRowName(ELureMovementState State)
{
	static const FName Names[NumStates] = { TEXT("Stand"), TEXT("Sprint"), TEXT("Crouch"), TEXT("Prone"), TEXT("Swim"), TEXT("SwimSprint") };
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

	// The optional columns: the climb rule (land: above the takeoff; swimming: above the water), the surface float,
	// the arms pull-back and the camera's exit time.
	auto WithExtras = [](FLureMovementRow Row, float ClimbMax, float ClimbRate, float FloatDepth, float PullBack, float ExitTime)
	{
		Row.ClimbMaxHeight = ClimbMax;
		Row.ClimbSpeed = ClimbRate;
		Row.SurfaceFloatDepth = FloatDepth;
		Row.ArmsPullBack = PullBack;
		Row.ExitTransitionTime = ExitTime;
		return Row;
	};

	switch (State)
	{
	case ELureMovementState::Swim:
		// T-026: the Stand capsule, eyes 18 cm above the water, no arms bob; Jump climbs out onto edges up to 60 cm.
		return WithExtras(WithBob(MakeRow(170.f, 700.f, 90.f, 34.f, 118.f, 0.3f, 1.6f, 0.f, false), 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f), 60.f, 300.f, 10.f, 0.f, 0.f);
	case ELureMovementState::SwimSprint:
		return WithExtras(WithBob(MakeRow(290.f, 900.f, 90.f, 34.f, 118.f, 0.3f, 3.0f, 0.f, false), 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f), 60.f, 300.f, 10.f, 0.f, 0.f);
	case ELureMovementState::Sprint:
		return WithExtras(WithBob(MakeRow(600.f, 2048.f, 90.f, 34.f, 165.f, 0.25f, 2.5f, 440.f, true), 3.0f, 1.8f, 1.2f, 1.5f, 0.f, 0.3f, 1.0f), 100.f, 400.f, 0.f, 0.f, 0.f);
	case ELureMovementState::Crouch:
		return WithExtras(WithBob(MakeRow(180.f, 1600.f, 55.f, 34.f, 95.f, 0.20f, 0.5f, 380.f, true), 0.5f, 0.9f, 0.9f, 0.3f, 0.f, 0.f, 1.0f), 100.f, 400.f, 0.f, 0.f, 0.f);
	case ELureMovementState::Prone:
		return WithExtras(WithBob(MakeRow(90.f, 1200.f, 26.f, 25.f, 35.f, 0.45f, 0.2f, 0.f, false), 0.4f, 1.6f, 2.0f, 0.3f, 1.5f, 1.2f, 0.85f), 0.f, 0.f, 0.f, 12.f, 0.42f);
	case ELureMovementState::Stand:
	default:
		return WithExtras(WithBob(MakeRow(350.f, 2048.f, 90.f, 34.f, 165.f, 0.25f, 1.0f, 420.f, true), 1.5f, 1.0f, 0.6f, 0.4f, 0.f, 0.f, 1.0f), 100.f, 400.f, 0.f, 0.f, 0.f);
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

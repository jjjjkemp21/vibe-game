// Lure: the fish you see fighting on the line (T-029). Pure rules.

#include "Fish/FightFishVisual.h"
#include "Fish/FishTypes.h"

namespace FightFishVisualPrivate
{
	static bool Finite(float Value) { return FMath::IsFinite(Value); }

	static FString RoleName(EFishAnimRole Role)
	{
		return StaticEnum<EFishAnimRole>()->GetNameStringByValue(static_cast<int64>(Role));
	}
}

bool FFishVisualRow::Validate(FString& OutProblem) const
{
	struct FNumber { const TCHAR* Name; float Value; float Min; float Max; };
	const FNumber Numbers[] = {
		{ TEXT("StrideBodyLengths"), StrideBodyLengths, 0.01f, 100.f },
		{ TEXT("MinPlayRate"), MinPlayRate, 0.f, 100.f },
		{ TEXT("MaxPlayRate"), MaxPlayRate, 0.f, 100.f },
		{ TEXT("OtherRateWeightExponent"), OtherRateWeightExponent, 0.f, 10.f },
		{ TEXT("RoleBlendTime"), RoleBlendTime, 0.f, 10.f },
		{ TEXT("HookSetThrashTime"), HookSetThrashTime, 0.f, 60.f },
		{ TEXT("DartRightStartTime"), DartRightStartTime, 0.f, 60.f },
		{ TEXT("ExhaustedPlayRate"), ExhaustedPlayRate, 0.f, 100.f },
		{ TEXT("ExhaustedAmplitudeScale"), ExhaustedAmplitudeScale, 0.f, 1.f },
		{ TEXT("ExhaustedRollDeg"), ExhaustedRollDeg, -180.f, 180.f },
		{ TEXT("MinScale"), MinScale, 0.01f, 100.f },
		{ TEXT("MaxScale"), MaxScale, 0.01f, 100.f },
		{ TEXT("DefaultBodyLengthCm"), DefaultBodyLengthCm, 1.f, 10000.f },
		{ TEXT("SurfaceDepth"), SurfaceDepth, 0.f, 10000.f },
		{ TEXT("DepthShare"), DepthShare, 0.f, 10.f },
		{ TEXT("MaxShownDepth"), MaxShownDepth, 0.f, 100000.f },
		{ TEXT("FloorClearance"), FloorClearance, -1.f, 10000.f },
		{ TEXT("AuthoritySmoothTime"), AuthoritySmoothTime, 0.f, 10.f },
		{ TEXT("ProxySmoothTime"), ProxySmoothTime, 0.f, 10.f },
		{ TEXT("SnapDistance"), SnapDistance, 0.f, 1000000.f },
		{ TEXT("RotationSmoothTime"), RotationSmoothTime, 0.f, 10.f },
		{ TEXT("MinFacingSpeed"), MinFacingSpeed, 0.f, 100000.f },
		{ TEXT("MaxPitchDeg"), MaxPitchDeg, 0.f, 89.f },
		{ TEXT("EscapeTime"), EscapeTime, 0.f, 60.f },
		{ TEXT("EscapeSpeed"), EscapeSpeed, 0.f, 100000.f },
		{ TEXT("EscapeSinkSpeed"), EscapeSinkSpeed, 0.f, 100000.f },
	};
	for (const FNumber& Number : Numbers)
	{
		if (!FightFishVisualPrivate::Finite(Number.Value) || Number.Value < Number.Min || Number.Value > Number.Max)
		{
			OutProblem = FString::Printf(TEXT("%s = %g is outside [%g, %g]"), Number.Name, Number.Value, Number.Min, Number.Max);
			return false;
		}
	}
	if (MinPlayRate > MaxPlayRate)
	{
		OutProblem = FString::Printf(TEXT("MinPlayRate %g > MaxPlayRate %g"), MinPlayRate, MaxPlayRate);
		return false;
	}
	if (MinScale > MaxScale)
	{
		OutProblem = FString::Printf(TEXT("MinScale %g > MaxScale %g"), MinScale, MaxScale);
		return false;
	}
	TSet<FName> Moves;
	for (const FFishMoveAnimRole& Entry : MoveRoles)
	{
		if (Entry.MoveId.IsNone())
		{
			OutProblem = TEXT("MoveRoles has an entry without a MoveId");
			return false;
		}
		bool bDuplicate = false;
		Moves.Add(Entry.MoveId, &bDuplicate);
		if (bDuplicate)
		{
			OutProblem = FString::Printf(TEXT("MoveRoles lists %s twice"), *Entry.MoveId.ToString());
			return false;
		}
	}
	TSet<EFishAnimRole> Roles;
	for (const FFishRoleTailBeat& Beat : RoleTailBeats)
	{
		bool bDuplicate = false;
		Roles.Add(Beat.Role, &bDuplicate);
		if (bDuplicate)
		{
			OutProblem = FString::Printf(TEXT("RoleTailBeats lists %s twice"), *FightFishVisualPrivate::RoleName(Beat.Role));
			return false;
		}
		if (!FightFishVisualPrivate::Finite(Beat.Hz) || Beat.Hz <= 0.f)
		{
			OutProblem = FString::Printf(TEXT("RoleTailBeats %s: Hz %g must be > 0"), *FightFishVisualPrivate::RoleName(Beat.Role), Beat.Hz);
			return false;
		}
	}
	return true;
}

FFishVisualRow FFishVisualRow::GetFallbackRow()
{
	// Mirrors data/tables/DT_FishVisual.json (Project.FishVisual.Data.FallbackMatchesSource checks it).
	FFishVisualRow Row;
	auto Move = [&Row](const TCHAR* Id, EFishAnimRole Role)
	{
		FFishMoveAnimRole Entry;
		Entry.MoveId = Id;
		Entry.Role = Role;
		Row.MoveRoles.Add(Entry);
	};
	Move(TEXT("Run"), EFishAnimRole::Run);
	Move(TEXT("Dive"), EFishAnimRole::Dive);
	Move(TEXT("Dart"), EFishAnimRole::Dart);
	Move(TEXT("Swim"), EFishAnimRole::SwimFast);
	Move(TEXT("Charge"), EFishAnimRole::SwimFast);
	Move(TEXT("Rest"), EFishAnimRole::SwimIdle);
	Move(TEXT("Sulk"), EFishAnimRole::Thrash);
	Row.UnknownMoveRole = EFishAnimRole::SwimFast;
	auto Beat = [&Row](EFishAnimRole Role, float Hz)
	{
		FFishRoleTailBeat Entry;
		Entry.Role = Role;
		Entry.Hz = Hz;
		Row.RoleTailBeats.Add(Entry);
	};
	Beat(EFishAnimRole::SwimIdle, 1.f);
	Beat(EFishAnimRole::SwimFast, 2.5f);
	Beat(EFishAnimRole::Run, 3.f);
	Beat(EFishAnimRole::Dive, 2.f);
	return Row; // every other number is the struct default (= the JSON)
}

float FFightFishVisual::WeightScale(float WeightKg, float ReferenceWeightKg, const FFishVisualRow& Row)
{
	if (!FMath::IsFinite(WeightKg) || !FMath::IsFinite(ReferenceWeightKg) || WeightKg <= 0.f || ReferenceWeightKg <= 0.f)
	{
		return 1.f;
	}
	const float Scale = FMath::Pow(WeightKg / ReferenceWeightKg, 1.f / 3.f);
	const float Lo = FMath::Max(0.01f, FMath::Min(Row.MinScale, Row.MaxScale));
	const float Hi = FMath::Max(Lo, Row.MaxScale);
	return FMath::Clamp(Scale, Lo, Hi);
}

EFishAnimRole FFightFishVisual::RoleForMove(const FFishVisualRow& Row, FName MoveId)
{
	for (const FFishMoveAnimRole& Entry : Row.MoveRoles)
	{
		if (Entry.MoveId == MoveId)
		{
			return Entry.Role;
		}
	}
	return Row.UnknownMoveRole;
}

float FFightFishVisual::TailBeatHz(const FFishVisualRow& Row, EFishAnimRole Role)
{
	for (const FFishRoleTailBeat& Beat : Row.RoleTailBeats)
	{
		if (Beat.Role == Role)
		{
			return FMath::IsFinite(Beat.Hz) ? FMath::Max(0.f, Beat.Hz) : 0.f;
		}
	}
	return 0.f;
}

FFishAnimState FFightFishVisual::ComputeAnimState(const FFishVisualRow& Row, const FFightFishAnimInput& In)
{
	const float AnimRate = (FMath::IsFinite(In.AnimRate) && In.AnimRate > 0.f) ? In.AnimRate : 1.f;
	const float Amplitude = FMath::IsFinite(In.AnimAmplitude) ? FMath::Clamp(In.AnimAmplitude, 0.f, 1.f) : 1.f;

	FFishAnimState State;
	State.RoleBlendTime = Row.RoleBlendTime;
	State.Amplitude = Amplitude;
	float FixedRate = -1.f; // >= 0: this rate, no speed or weight rule

	switch (In.Phase)
	{
	case EFightFishPhase::Landed:
		State.Role = EFishAnimRole::Flop;
		State.Amplitude = 1.f; // the dock clearance of Landed_Flop holds only at alpha 1 (SK_Fish.anim.md)
		break;
	case EFightFishPhase::Escaping:
		State.Role = EFishAnimRole::SwimFast;
		break;
	default:
		if (In.SecondsSinceHook < Row.HookSetThrashTime)
		{
			State.Role = EFishAnimRole::Thrash;
		}
		else if (In.bExhausted)
		{
			State.Role = EFishAnimRole::SwimIdle;
			State.Amplitude = Amplitude * Row.ExhaustedAmplitudeScale;
			FixedRate = Row.ExhaustedPlayRate;
		}
		else
		{
			State.Role = RoleForMove(Row, In.MoveId);
		}
		break;
	}

	if (FixedRate >= 0.f)
	{
		State.PlayRate = FixedRate;
	}
	else if (const float Hz = TailBeatHz(Row, State.Role); Hz > 0.f)
	{
		const float Length = FMath::Max(1.f, FMath::IsFinite(In.BodyLengthCm) ? In.BodyLengthCm : Row.DefaultBodyLengthCm);
		const float Speed = FMath::IsFinite(In.SpeedCmS) ? FMath::Max(0.f, In.SpeedCmS) : 0.f;
		const float Rate = AnimRate * Speed / (FMath::Max(0.01f, Row.StrideBodyLengths) * Length * Hz);
		State.PlayRate = FMath::Clamp(Rate, Row.MinPlayRate, FMath::Max(Row.MinPlayRate, Row.MaxPlayRate));
	}
	else
	{
		float WeightFactor = 1.f;
		if (FMath::IsFinite(In.WeightKg) && FMath::IsFinite(In.ReferenceWeightKg) && In.WeightKg > 0.f && In.ReferenceWeightKg > 0.f)
		{
			WeightFactor = FMath::Pow(In.ReferenceWeightKg / In.WeightKg, Row.OtherRateWeightExponent);
		}
		State.PlayRate = AnimRate * WeightFactor;
	}
	State.DartStartTime = (State.Role == EFishAnimRole::Dart && In.bDartRight) ? Row.DartRightStartTime : 0.f;
	return State;
}

float FFightFishVisual::ShownDepth(const FFishVisualRow& Row, float FightDepthCm)
{
	const float Depth = FMath::IsFinite(FightDepthCm) ? FMath::Max(0.f, FightDepthCm) : 0.f;
	return Row.SurfaceDepth + FMath::Min(Row.DepthShare * Depth, Row.MaxShownDepth);
}

FVector FFightFishVisual::TargetLocation(const FFishVisualRow& Row, const FFightFishView& View, const FVector& Forward, float MouthOffsetCm, float FloorZ)
{
	const FVector Flat = Forward.GetSafeNormal2D();
	FVector Target = FVector(View.LineEnd.X, View.LineEnd.Y, 0.f) - Flat * (FMath::IsFinite(MouthOffsetCm) ? MouthOffsetCm : 0.f);
	Target.Z = View.WaterZ - ShownDepth(Row, View.DepthCm);
	if (Row.FloorClearance >= 0.f && FloorZ > -UE_BIG_NUMBER * 0.5f)
	{
		// Shallow water: stay above the bottom, but never above the surface.
		Target.Z = FMath::Min(FMath::Max(Target.Z, FloorZ + Row.FloorClearance), View.WaterZ);
	}
	return Target;
}

FVector FFightFishVisual::EscapeStep(const FFishVisualRow& Row, const FVector& Location, const FVector& Direction, float DeltaTime, float WaterZ, float FloorZ)
{
	const float Dt = FMath::IsFinite(DeltaTime) ? FMath::Max(0.f, DeltaTime) : 0.f;
	FVector Next = Location + (Direction.GetSafeNormal2D() * Row.EscapeSpeed + FVector::DownVector * Row.EscapeSinkSpeed) * Dt;
	if (Row.FloorClearance >= 0.f && FMath::IsFinite(FloorZ) && FloorZ > -UE_BIG_NUMBER * 0.5f)
	{
		const double MinZ = FloorZ + Row.FloorClearance;
		if (Next.Z < MinZ)
		{
			// Sinking into shallow sand: glide along the bottom instead (never pushed out of the water).
			Next.Z = FMath::IsFinite(WaterZ) ? FMath::Min(MinZ, static_cast<double>(WaterZ)) : MinZ;
		}
	}
	return Next;
}

FRotator FFightFishVisual::FacingRotation(const FFishVisualRow& Row, const FVector& Velocity, const FVector& FishLocation, const FVector& PlayerLocation,
	const FRotator& Current, bool bExhausted)
{
	FRotator Out(0.f, Current.Yaw, 0.f);
	const float Horizontal = static_cast<float>(Velocity.Size2D());
	if (Horizontal > Row.MinFacingSpeed)
	{
		Out.Yaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y, Velocity.X)));
		const float Climb = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Velocity.Z, static_cast<double>(Horizontal))));
		Out.Pitch = FMath::Clamp(Climb, -Row.MaxPitchDeg, Row.MaxPitchDeg);
	}
	else
	{
		const FVector Away = (FishLocation - PlayerLocation).GetSafeNormal2D();
		if (!Away.IsNearlyZero())
		{
			Out.Yaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Away.Y, Away.X)));
		}
	}
	Out.Roll = bExhausted ? Row.ExhaustedRollDeg : 0.f;
	return Out;
}

float FFightFishVisual::SmoothAlpha(float DeltaTime, float TimeConstant)
{
	if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.f)
	{
		return 0.f;
	}
	if (!FMath::IsFinite(TimeConstant) || TimeConstant <= 0.f)
	{
		return 1.f;
	}
	return 1.f - FMath::Exp(-DeltaTime / TimeConstant);
}

bool FFightFishVisual::ValidateSpeciesLook(const FFishSpeciesRow& Species, FString& OutProblem)
{
	if (!FMath::IsFinite(Species.AnimAmplitude) || Species.AnimAmplitude < 0.f || Species.AnimAmplitude > 1.f)
	{
		OutProblem = FString::Printf(TEXT("AnimAmplitude %g is outside [0, 1]"), Species.AnimAmplitude);
		return false;
	}
	if (!FMath::IsFinite(Species.AnimRate) || Species.AnimRate <= 0.f)
	{
		OutProblem = FString::Printf(TEXT("AnimRate %g must be > 0"), Species.AnimRate);
		return false;
	}
	return true;
}

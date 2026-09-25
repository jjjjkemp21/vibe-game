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
		{ TEXT("FreshAmplitudeScale"), FreshAmplitudeScale, 0.f, 1.f },
		{ TEXT("TiredAmplitudeScale"), TiredAmplitudeScale, 0.f, 1.f },
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
		{ TEXT("RunSwingDeg"), RunSwingDeg, 0.f, 80.f },
		{ TEXT("BodyAngleDeg"), BodyAngleDeg, 0.f, 80.f },
		{ TEXT("RunSwingFullSpeed"), RunSwingFullSpeed, 1.f, 100000.f },
		{ TEXT("MouthMaxLagCm"), MouthMaxLagCm, 0.f, 1000.f },
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
		if (Entry.Role == EFishAnimRole::Curled)
		{
			OutProblem = FString::Printf(TEXT("MoveRoles %s: Curled is the cooler's held pose, not a fight role"), *Entry.MoveId.ToString());
			return false;
		}
	}
	if (UnknownMoveRole == EFishAnimRole::Curled)
	{
		OutProblem = TEXT("UnknownMoveRole: Curled is the cooler's held pose, not a fight role");
		return false;
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
	TSet<EFishAnimRole> StaminaRoles;
	for (const FFishRoleStaminaRate& Rate : RoleStaminaRates)
	{
		bool bDuplicate = false;
		StaminaRoles.Add(Rate.Role, &bDuplicate);
		if (bDuplicate)
		{
			OutProblem = FString::Printf(TEXT("RoleStaminaRates lists %s twice"), *FightFishVisualPrivate::RoleName(Rate.Role));
			return false;
		}
		for (const float Value : { Rate.FreshRate, Rate.TiredRate })
		{
			if (!FightFishVisualPrivate::Finite(Value) || Value < 0.f || Value > 100.f)
			{
				OutProblem = FString::Printf(TEXT("RoleStaminaRates %s: rate %g is outside [0, 100]"), *FightFishVisualPrivate::RoleName(Rate.Role), Value);
				return false;
			}
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
	Move(TEXT("Sulk"), EFishAnimRole::SwimIdle); // T-059a (art S3): a sulking fish holds, it does not thrash
	Move(TEXT("Shake"), EFishAnimRole::Thrash); // T-049: the Run pattern's opener is a head shake
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
	auto Stamina = [&Row](EFishAnimRole Role, float Fresh, float Tired)
	{
		FFishRoleStaminaRate Entry;
		Entry.Role = Role;
		Entry.FreshRate = Fresh;
		Entry.TiredRate = Tired;
		Row.RoleStaminaRates.Add(Entry);
	};
	// T-059a, art's gate A table (Saved/AgentLogs/tasks/S3-fish/gateA_playrate_table.md).
	Stamina(EFishAnimRole::Run, 1.f, 0.6f);
	Stamina(EFishAnimRole::SwimFast, 1.f, 0.6f);
	Stamina(EFishAnimRole::Dive, 1.f, 0.6f);
	Stamina(EFishAnimRole::Dart, 1.f, 0.7f);
	Stamina(EFishAnimRole::Thrash, 1.f, 0.7f);
	Stamina(EFishAnimRole::SwimIdle, 1.f, 0.8f);
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

float FFightFishVisual::StaminaRate(const FFishVisualRow& Row, EFishAnimRole Role, float Stamina01)
{
	const float Stamina = FMath::IsFinite(Stamina01) ? FMath::Clamp(Stamina01, 0.f, 1.f) : 1.f;
	for (const FFishRoleStaminaRate& Rate : Row.RoleStaminaRates)
	{
		if (Rate.Role == Role)
		{
			const float Fresh = FMath::IsFinite(Rate.FreshRate) ? FMath::Max(0.f, Rate.FreshRate) : 1.f;
			const float Tired = FMath::IsFinite(Rate.TiredRate) ? FMath::Max(0.f, Rate.TiredRate) : Fresh;
			return FMath::Lerp(Tired, Fresh, Stamina);
		}
	}
	return 1.f;
}

FFishAnimState FFightFishVisual::ComputeAnimState(const FFishVisualRow& Row, const FFightFishAnimInput& In)
{
	const float AnimRate = (FMath::IsFinite(In.AnimRate) && In.AnimRate > 0.f) ? In.AnimRate : 1.f;
	const float Amplitude = FMath::IsFinite(In.AnimAmplitude) ? FMath::Clamp(In.AnimAmplitude, 0.f, 1.f) : 1.f;
	const float Stamina = FMath::IsFinite(In.Stamina01) ? FMath::Clamp(In.Stamina01, 0.f, 1.f) : 1.f;

	FFishAnimState State;
	State.RoleBlendTime = Row.RoleBlendTime;
	State.Amplitude = Amplitude;
	float FixedRate = -1.f; // >= 0: this rate, no speed or weight rule
	bool bEffort = false;   // T-059a: the fight's effort rule (stamina, not speed)

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
		if (!In.bExhausted)
		{
			bEffort = true;
			const float Scale = FMath::Lerp(Row.TiredAmplitudeScale, Row.FreshAmplitudeScale, Stamina);
			State.Amplitude = FMath::Clamp(Amplitude * (FMath::IsFinite(Scale) ? Scale : 1.f), 0.f, 1.f);
		}
		break;
	}

	float WeightFactor = 1.f;
	if (FMath::IsFinite(In.WeightKg) && FMath::IsFinite(In.ReferenceWeightKg) && In.WeightKg > 0.f && In.ReferenceWeightKg > 0.f)
	{
		WeightFactor = FMath::Pow(In.ReferenceWeightKg / In.WeightKg, Row.OtherRateWeightExponent);
	}
	if (FixedRate >= 0.f)
	{
		State.PlayRate = FixedRate;
	}
	else if (bEffort)
	{
		State.PlayRate = AnimRate * WeightFactor * StaminaRate(Row, State.Role, Stamina);
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
	return CenterForMouth(MouthTarget(Row, View, FloorZ), Forward, MouthOffsetCm);
}

FVector FFightFishVisual::MouthTarget(const FFishVisualRow& Row, const FFightFishView& View, float FloorZ)
{
	FVector Target(View.LineEnd.X, View.LineEnd.Y, View.WaterZ - ShownDepth(Row, View.DepthCm));
	if (Row.FloorClearance >= 0.f && FloorZ > -UE_BIG_NUMBER * 0.5f)
	{
		// Shallow water: stay above the bottom, but never above the surface.
		Target.Z = FMath::Min(FMath::Max(Target.Z, FloorZ + Row.FloorClearance), View.WaterZ);
	}
	return Target;
}

FVector FFightFishVisual::CenterForMouth(const FVector& MouthPoint, const FVector& Forward, float MouthOffsetCm)
{
	// The horizontal part of the 3D facing: a pitched fish's nose stays right over the line end.
	const FVector Unit = Forward.GetSafeNormal();
	const double Offset = FMath::IsFinite(MouthOffsetCm) ? MouthOffsetCm : 0.f;
	return FVector(MouthPoint.X - Unit.X * Offset, MouthPoint.Y - Unit.Y * Offset, MouthPoint.Z);
}

FVector FFightFishVisual::StepMouth(const FFishVisualRow& Row, const FVector& MouthPoint, const FVector& Target, float DeltaTime, float SmoothTime)
{
	FVector Next = FMath::Lerp(MouthPoint, Target, static_cast<double>(SmoothAlpha(DeltaTime, SmoothTime)));
	const double MaxLag = FMath::IsFinite(Row.MouthMaxLagCm) ? FMath::Max(0.f, Row.MouthMaxLagCm) : 0.0;
	const FVector2D Lag(Next.X - Target.X, Next.Y - Target.Y);
	const double LagSize = Lag.Size();
	if (LagSize > MaxLag)
	{
		const FVector2D Kept = Lag * (MaxLag / LagSize);
		Next.X = Target.X + Kept.X;
		Next.Y = Target.Y + Kept.Y;
	}
	return Next;
}

namespace FightFishVisualPrivate
{
	/** Yaw of the direction mouth -> player, degrees; Fallback when they are on top of each other. */
	static float YawToPlayer(const FVector& MouthLocation, const FVector& PlayerLocation, float Fallback)
	{
		const FVector ToPlayer = (PlayerLocation - MouthLocation).GetSafeNormal2D();
		return ToPlayer.IsNearlyZero() ? Fallback : static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(ToPlayer.Y, ToPlayer.X)));
	}

	static float MaxSwing(const FFishVisualRow& Row)
	{
		return FMath::IsFinite(Row.RunSwingDeg) ? FMath::Clamp(Row.RunSwingDeg, 0.f, 80.f) : 0.f;
	}

	/** T-075: the body angle off the line when the fish does not swing, [0, 80]. */
	static float BodyAngle(const FFishVisualRow& Row)
	{
		return FMath::IsFinite(Row.BodyAngleDeg) ? FMath::Clamp(Row.BodyAngleDeg, 0.f, 80.f) : 0.f;
	}
}

float FFightFishVisual::MaxBodyAngleDeg(const FFishVisualRow& Row)
{
	return FMath::Min(80.f, FightFishVisualPrivate::BodyAngle(Row) + FightFishVisualPrivate::MaxSwing(Row));
}

FRotator FFightFishVisual::FightFacing(const FFishVisualRow& Row, const FVector& MouthVelocity, const FVector& MouthLocation, const FVector& PlayerLocation,
	const FRotator& Current, bool bExhausted, bool bMoveSwims, float MoveSwimSide)
{
	const float BaseYaw = FightFishVisualPrivate::YawToPlayer(MouthLocation, PlayerLocation, static_cast<float>(Current.Yaw));
	// The side the body lies to, in yaw off the line (+ = larger yaw): the side it already leans to; exactly away from the
	// player (a fish that just spawned faces away) = +, so every machine starts on the same side (T-075).
	const float Lean = FMath::FindDeltaAngleDegrees(BaseYaw, static_cast<float>(Current.Yaw));
	float Sign = (FMath::Abs(Lean) > 179.5f || Lean >= 0.f) ? 1.f : -1.f;
	float Swing = 0.f;
	const bool bFollowMove = bMoveSwims && !bExhausted;
	if (bFollowMove && FMath::IsFinite(MoveSwimSide))
	{
		// T-048b: the move's own swim sets the swing. + = the player's right = toward smaller yaw from the fish's view of the
		// player (a fish at +X from the player faces yaw 180; its head turns toward +Y, the player's right, at yaw < 180).
		const float Side = FMath::Clamp(MoveSwimSide, -1.f, 1.f);
		if (Side != 0.f)
		{
			Sign = Side > 0.f ? -1.f : 1.f;
		}
		Swing = FMath::Abs(Side) * FightFishVisualPrivate::MaxSwing(Row);
	}
	const FVector Horizontal(MouthVelocity.X, MouthVelocity.Y, 0.f);
	const float Speed = static_cast<float>(Horizontal.Size());
	const bool bMoving = FMath::IsFinite(Speed) && FMath::IsFinite(MouthVelocity.Z) && Speed > Row.MinFacingSpeed;
	if (bMoving && !bExhausted && !bFollowMove)
	{
		// Sideways component of the swim (+ = toward larger yaw, i.e. from X toward Y).
		const FVector ToPlayer = FRotator(0.f, BaseYaw, 0.f).Vector();
		const FVector Side(-ToPlayer.Y, ToPlayer.X, 0.f);
		const float Lateral = static_cast<float>(FVector::DotProduct(Horizontal, Side));
		if (FMath::Abs(Lateral) > 0.25f * Speed)
		{
			Sign = Lateral < 0.f ? -1.f : 1.f; // the head turns toward the side it swims to
		}
		Swing = FightFishVisualPrivate::MaxSwing(Row) * FMath::Clamp(Speed / FMath::Max(1.f, Row.RunSwingFullSpeed), 0.f, 1.f);
	}
	// T-075: never straight away from the rod (the fish would hide behind the bobber): BodyAngleDeg, plus the swing.
	FRotator Out(0.f, BaseYaw + Sign * FMath::Min(80.f, FightFishVisualPrivate::BodyAngle(Row) + Swing), 0.f);
	if (bMoving)
	{
		// The end that leads the swim follows the climb: head first (reeled in) = nose along the climb, tail first = against it.
		const float Climb = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(MouthVelocity.Z, static_cast<double>(Speed))));
		const float Lead = FVector::DotProduct(Horizontal, Out.Vector()) >= 0.0 ? 1.f : -1.f;
		Out.Pitch = FMath::Clamp(Lead * Climb, -Row.MaxPitchDeg, Row.MaxPitchDeg);
	}
	Out.Roll = bExhausted ? Row.ExhaustedRollDeg : 0.f;
	return Out;
}

FRotator FFightFishVisual::ClampToLine(const FFishVisualRow& Row, const FRotator& Rotation, const FVector& MouthLocation, const FVector& PlayerLocation)
{
	const float BaseYaw = FightFishVisualPrivate::YawToPlayer(MouthLocation, PlayerLocation, static_cast<float>(Rotation.Yaw));
	const float Max = MaxBodyAngleDeg(Row);
	FRotator Out = Rotation;
	Out.Yaw = BaseYaw + FMath::Clamp(FMath::FindDeltaAngleDegrees(BaseYaw, static_cast<float>(Rotation.Yaw)), -Max, Max);
	return Out;
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

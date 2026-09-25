// Lure: rod steering during the reel fight (T-028), the ULureFishingComponent side (kept out of LureFishingComponent.cpp):
// look input -> rod aim, the reel speed steps, the compact input to the server, the camera that follows the fish and the rod,
// the eased aim for the arms and for other players, the placeholder HUD lines.
// Pure helpers: LureRodControl.h. What the rod does to the fish and the line: FishFight.h. Spec: docs/specs/reel-fight-rules.md.

#include "Fishing/LureFishingComponent.h"
#include "Camera/CameraComponent.h"
#include "Character/FPArmsAnimInstance.h"
#include "Character/LureInputSubsystem.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"

bool ULureFishingComponent::IsSteeringRod() const
{
	return IsOwnerLocallyControlled() && NetState.State == ELureFishingState::Hooked && FightNet.bActive;
}

bool ULureFishingComponent::SyncRodAimToFight()
{
	if (!IsSteeringRod())
	{
		return false;
	}
	if (!bAimFightValid || AimFightId != FightNet.FightId)
	{
		// A new fight: the rod starts level and centered (so does the server's input), and the input goes out at once.
		RodAim.Reset();
		AimFightId = FightNet.FightId;
		bAimFightValid = true;
		bFightInputDirty = true;
	}
	return true;
}

bool ULureFishingComponent::ConsumeLookInput(float YawDegrees, float PitchDegrees)
{
	if (!SyncRodAimToFight())
	{
		return false;
	}
	RodAim.AddLookInput(YawDegrees, PitchDegrees, GetFightTuning());
	return true;
}

void ULureFishingComponent::StepReelSpeed(int32 Delta)
{
	if (Delta == 0 || !SyncRodAimToFight())
	{
		return;
	}
	const int32 Next = FMath::Clamp(GetReelStep() + Delta, 0, FLureFight::NumReelSteps(GetFightTuning()) - 1);
	if (Next != GetReelStep())
	{
		LocalReelStep = Next;
		bFightInputDirty = true; // a step change goes out right away
	}
}

int32 ULureFishingComponent::GetReelStep() const
{
	// The owner's choice is kept from fight to fight (a reel keeps its setting); everyone else sees the server's.
	return FLureFight::ClampReelStep(IsOwnerLocallyControlled() ? LocalReelStep : static_cast<int32>(FightNet.ReelStep), GetFightTuning());
}

FVector2D ULureFishingComponent::GetRodAim() const
{
	if (IsSteeringRod())
	{
		if (!bAimFightValid || AimFightId != FightNet.FightId)
		{
			return FVector2D::ZeroVector; // a new fight whose aim is not reset yet (next tick): level and centered
		}
		const FLureFishFightRow& Tuning = GetFightTuning();
		return FVector2D(RodAim.GetYaw01(Tuning), RodAim.GetPitch01(Tuning));
	}
	if (!IsOwnerLocallyControlled() && FightNet.bActive && NetState.State == ELureFishingState::Hooked)
	{
		return FVector2D(FightNet.RodYaw, FightNet.RodPitch);
	}
	return FVector2D::ZeroVector;
}

void ULureFishingComponent::AuthoritySetFightInput(uint8 FightId, float RodPitch, float RodYaw, int32 ReelStep)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !FightNet.bActive || NetState.State != ELureFishingState::Hooked || FightId != FightNet.FightId)
	{
		return; // no fight on, or a late packet from an earlier fight
	}
	FLureFightInput Requested;
	Requested.RodPitch = RodPitch;
	Requested.RodYaw = RodYaw;
	Requested.ReelStep = ReelStep;
	const FLureFightInput Clean = FLureFight::SanitizeInput(Requested, Fight.Tuning);
	ServerRodPitch = Clean.RodPitch;
	ServerRodYaw = Clean.RodYaw;
	// T-028b (O6): reel-step changes are rate limited on the server; the latest held-back step applies when the limit allows.
	if (Clean.ReelStep == FLureFight::ClampReelStep(ServerReelStep, Fight.Tuning))
	{
		ServerReelStep = Clean.ReelStep; // the step in use (a first packet names the default step explicitly): no change, no token
		ServerPendingReelStep = INDEX_NONE; // back to the step in use: nothing to apply
		return;
	}
	ServerPendingReelStep = Clean.ReelStep;
	ApplyPendingReelStep(GetFishingTime());
}

void ULureFishingComponent::ApplyPendingReelStep(double Now)
{
	if (ServerPendingReelStep == INDEX_NONE)
	{
		return;
	}
	const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
	const float Rate = FMath::IsFinite(Settings->FightReelStepsPerSecond) ? Settings->FightReelStepsPerSecond : 0.f;
	if (Rate > 0.f)
	{
		const float Burst = static_cast<float>(FMath::Max(1, Settings->FightReelStepBurst));
		if (ServerReelStepTokens < 0.f)
		{
			ServerReelStepTokens = Burst;
		}
		else
		{
			ServerReelStepTokens = FMath::Min(Burst, ServerReelStepTokens + static_cast<float>(FMath::Max(0.0, Now - ServerReelStepTokenTime)) * Rate);
		}
		ServerReelStepTokenTime = Now;
		if (ServerReelStepTokens < 1.f - 1.0e-4f)
		{
			return; // over the limit: the step waits (the latest one) until a token is back
		}
		ServerReelStepTokens = FMath::Max(0.f, ServerReelStepTokens - 1.f);
	}
	ServerReelStep = ServerPendingReelStep;
	ServerPendingReelStep = INDEX_NONE;
}

FLureFightInput ULureFishingComponent::GetServerFightInput() const
{
	FLureFightInput Input;
	Input.bReeling = bServerReeling;
	Input.RodPitch = ServerRodPitch;
	Input.RodYaw = ServerRodYaw;
	Input.ReelStep = ServerReelStep;
	return Input;
}

void ULureFishingComponent::ServerSetFightInput_Implementation(uint8 FightId, uint8 RodPitch, uint8 RodYaw, uint8 ReelStep)
{
	AuthoritySetFightInput(FightId, FLureRodControl::UnpackAxis(RodPitch), FLureRodControl::UnpackAxis(RodYaw), ReelStep);
}

void ULureFishingComponent::UpdateRodSteering(float DeltaTime)
{
	if (!SyncRodAimToFight())
	{
		return;
	}
	UpdateFightInputSend();
	UpdateFightCamera(DeltaTime);
}

void ULureFishingComponent::UpdateFightInputSend()
{
	const FLureFishFightRow& Tuning = GetFightTuning();
	const uint8 Packet[4] = { FightNet.FightId, FLureRodControl::PackAxis(RodAim.GetPitch01(Tuning)), FLureRodControl::PackAxis(RodAim.GetYaw01(Tuning)),
		static_cast<uint8>(GetReelStep()) };
	const bool bChanged = FMemory::Memcmp(Packet, SentFightInput, sizeof(Packet)) != 0;
	const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
	const double Since = GetLocalTime() - FightInputSentTime;
	if (!bFightInputDirty && !(bChanged && Since >= Settings->FightInputSendSeconds) && Since < Settings->FightInputResendSeconds)
	{
		return;
	}
	FMemory::Memcpy(SentFightInput, Packet, sizeof(Packet));
	FightInputSentTime = GetLocalTime();
	bFightInputDirty = false;
	ServerSetFightInput(Packet[0], Packet[1], Packet[2], Packet[3]); // on a listen server / standalone this runs right here
}

void ULureFishingComponent::UpdateFightCamera(float DeltaTime)
{
	const APawn* Pawn = GetPawn();
	APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!Controller || Controller->IsLookInputIgnored())
	{
		return;
	}
	const FLureFishFightRow& Tuning = GetFightTuning();
	const UCameraComponent* Camera = GetOwnerCamera();
	const FVector Eye = Camera ? Camera->GetComponentLocation() : GetEyeLocation();
	const FVector2D Aim = GetRodAim();
	const FRotator Target = FLureRodControl::CameraTarget(Eye, GetBobberLocation(), Aim.Y, Aim.X, Tuning);
	Controller->SetControlRotation(FLureRodControl::CameraStep(Controller->GetControlRotation(), Target, DeltaTime, Tuning));
}

void ULureFishingComponent::UpdateRodAimVisual(float DeltaTime)
{
	RodAimVisual = FLureRodControl::EaseAim(RodAimVisual, GetRodAim(), DeltaTime, GetFightTuning().RodAimBlendTime);
}

FVector2D ULureFishingComponent::GetViewRodAim(float FullYawDeg) const
{
	// T-075b: the owner's arms and rod ride the camera, so the camera's turn never moves them on screen; their own side swing
	// does. Cap it (cosmetic: GetRodAim, the fight and the HUD keep the full aim).
	return IsOwnerLocallyControlled() ? FLureRodControl::CapViewAim(RodAimVisual, FullYawDeg, GetFightTuning()) : RodAimVisual;
}

FVector2D ULureFishingComponent::GetRodAimForAnimation() const
{
	return GetViewRodAim(GetFightTuning().RodAimSideDeg);
}

bool ULureFishingComponent::ArmsPlayRodAim() const
{
	const ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(GetOwner());
	const USkeletalMeshComponent* Arms = Lure ? Lure->GetFirstPersonArms() : nullptr;
	const UFPArmsAnimInstance* Anim = Arms ? Cast<UFPArmsAnimInstance>(Arms->GetAnimInstance()) : nullptr;
	return Anim && Anim->bRodAimOffsetInGraph;
}

void ULureFishingComponent::BindRodInput(UEnhancedInputComponent& Input)
{
	UInputAction* Faster = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::ReelFaster);
	UInputAction* Slower = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::ReelSlower);
	if (!Faster || !Slower)
	{
		UE_LOG(LogLureFishing, Warning, TEXT("%s: the ReelFaster/ReelSlower input actions are missing (ULureInputSubsystem not running); no reel speed keys."), *GetNameSafe(GetOwner()));
		return;
	}
	Input.BindAction(Faster, ETriggerEvent::Started, this, &ULureFishingComponent::PressReelFaster);
	Input.BindAction(Slower, ETriggerEvent::Started, this, &ULureFishingComponent::PressReelSlower);
}

void ULureFishingComponent::AppendRodHudLines(TArray<FString>& Lines) const
{
	// Placeholder text (UI rule): the rod angle, the reel step and which way the fish runs.
	constexpr float Zone = 1.f / 3.f;
	const FVector2D Aim = GetRodAim();
	FString Rod = FString::Printf(TEXT("Rod: %s"), *FLureRodControl::DescribeRod(Aim.Y, Aim.X));
	const float Side = FLureFight::SideScore(Aim.X, FLureRodControl::DirectionFromRunSide(FightNet.RunSide));
	if (Side > Zone)
	{
		Rod += TEXT("  (turning it)");
	}
	else if (Side < -Zone)
	{
		Rod += TEXT("  (same way: losing line)");
	}
	Lines.Add(Rod);
	Lines.Add(FLureRodControl::ReelText(GetReelStep(), FLureFight::NumReelSteps(GetFightTuning())));
	const FString Hint = FLureRodControl::RunHint(FightNet.RunSide);
	if (!Hint.IsEmpty())
	{
		Lines.Add(Hint);
	}
}

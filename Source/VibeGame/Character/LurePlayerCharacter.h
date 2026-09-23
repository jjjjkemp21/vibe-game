// Lure: first-person player character (T-004).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Character/LureArmsBob.h"
#include "Character/LureMovementTypes.h"
#include "LurePlayerCharacter.generated.h"

class APlayerController;
class UAnimInstance;
class UAnimSequenceBase;
class UCameraComponent;
class ULureCharacterMovementComponent;
class ULureFishingComponent;
class UMaterialInterface;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMeshComponent;
struct FInputActionValue;

/**
 *  Lure's first-person player: camera at eye height, owner-only arms, walk / sprint / jump / crouch / prone.
 *
 *  - Movement, stances and their networking live in ULureCharacterMovementComponent (tuning from DT_Movement).
 *  - The camera eases between the stances' eye heights (EyeHeight is measured from the feet) and never pops when
 *    the capsule resizes; GetPawnViewLocation (what AI will use for sight lines) is the camera.
 *  - Input: the shared Enhanced Input actions from ULureInputSubsystem (created in C++, keys in DefaultGame.ini).
 *  - Other players see a placeholder cylinder body (owner-no-see) until a real body exists.
 *
 *  Thin Blueprint children may set asset references and defaults (arms mesh, component offsets); no logic.
 */
UCLASS(Blueprintable)
class ALurePlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:

	ALurePlayerCharacter(const FObjectInitializer& ObjectInitializer);

	// ---- Components ----

	UFUNCTION(BlueprintPure, Category="Lure|Character")
	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }

	UFUNCTION(BlueprintPure, Category="Lure|Character")
	USkeletalMeshComponent* GetFirstPersonArms() const { return FirstPersonArms; }

	UFUNCTION(BlueprintPure, Category="Lure|Character")
	UStaticMeshComponent* GetPlaceholderBody() const { return PlaceholderBody; }

	UFUNCTION(BlueprintPure, Category="Lure|Character")
	ULureCharacterMovementComponent* GetLureMovement() const;

	/** Rod, cast, bobber, bite and hook (T-006). */
	UFUNCTION(BlueprintPure, Category="Lure|Character")
	ULureFishingComponent* GetFishing() const { return Fishing; }

	/**
	 *  First-person arms mesh (skeleton SKEL_FPArms), loaded at BeginPlay if the FirstPersonArms component has no mesh yet.
	 *  A missing asset means no arms (logged, not an error). SK_FPArms' origin is the eye point: zero offset under the camera.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lure|First Person")
	TSoftObjectPtr<USkeletalMesh> FirstPersonArmsMesh;

	/** Arms animation Blueprint (ABP_FPArms, child of UFPArmsAnimInstance). Missing = the arms stay in their bind pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lure|First Person")
	TSoftClassPtr<UAnimInstance> FirstPersonArmsAnimClass;

	/** Additive dip played in StanceAdditiveSlot on stance changes and landings (A_FPArms_StanceDip). Missing = no dip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lure|First Person")
	TSoftObjectPtr<UAnimSequenceBase> StanceDipAnimation;

	/** Slot (group "Additive" on SKEL_FPArms) the stance dip plays in, so it never interrupts DefaultSlot montages. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Lure|First Person")
	FName StanceAdditiveSlot;

	/** Radius of the probe that keeps the camera out of a ceiling while it eases down (about the near clip plane), cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Lure|First Person", meta=(ClampMin="0"))
	float CameraHeadroomRadius = 10.f;

	/** Show the placeholder body to other players. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Lure|Body")
	bool bShowPlaceholderBody = true;

	/** Placeholder body color (ART_STYLE sleeve olive #7C8A63). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Lure|Body")
	FLinearColor PlaceholderBodyColor;

	// ---- Requests (what the input handlers call; the wishes reach the server through the movement component) ----

	/** Ask for a posture. Stand/Crouch/Prone replace any queued request; Prone is refused in the air. A blocked stand-up stays queued until there is room. */
	UFUNCTION(BlueprintCallable, Category="Lure|Movement")
	void RequestStance(ELureStance Stance);

	/** Crouch key (toggle mode): Stand/Prone -> Crouch, Crouch -> Stand. */
	UFUNCTION(BlueprintCallable, Category="Lure|Movement")
	void ToggleCrouch();

	/** Prone key (toggle mode): Stand/Crouch -> Prone, Prone -> Stand. */
	UFUNCTION(BlueprintCallable, Category="Lure|Movement")
	void ToggleProne();

	/** Sprint wish. Pressing it while crouched asks to stand (queued under a ceiling); while prone it does nothing until you stand. */
	UFUNCTION(BlueprintCallable, Category="Lure|Movement")
	void SetSprintRequested(bool bRequested);

	UFUNCTION(BlueprintCallable, Category="Lure|Movement")
	void ToggleSprint();

	/** Movement input (X = right, Y = forward, -1..1). */
	UFUNCTION(BlueprintCallable, Category="Lure|Input")
	void DoMove(float Right, float Forward);

	/** Look input in degrees (yaw right, pitch up). */
	UFUNCTION(BlueprintCallable, Category="Lure|Input")
	void DoLook(float YawDegrees, float PitchDegrees);

	UFUNCTION(BlueprintCallable, Category="Lure|Input")
	void DoJumpStart();

	UFUNCTION(BlueprintCallable, Category="Lure|Input")
	void DoJumpEnd();

	// ---- State ----

	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	ELureStance GetStance() const;

	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	ELureStance GetRequestedStance() const;

	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	bool IsSprinting() const;

	UFUNCTION(BlueprintPure, Category="Lure|Movement")
	bool IsProne() const { return bIsProne; }

	/** Camera height above the feet right now (eases toward GetTargetEyeHeight), cm. */
	UFUNCTION(BlueprintPure, Category="Lure|First Person")
	float GetCurrentEyeHeight() const { return CurrentEyeHeight; }

	/** EyeHeight of the DT_Movement row in use, cm above the feet. */
	UFUNCTION(BlueprintPure, Category="Lure|First Person")
	float GetTargetEyeHeight() const;

	/** Smoothstep ease From -> To; Elapsed >= Duration or Duration <= 0 gives To. Pure (camera blend; tests). */
	static float EvaluateEyeBlend(float From, float To, float Elapsed, float Duration);

	// ---- First-person arms (cosmetic, local player only) ----

	/** True while the rod is in hand (steadier bob, HoldRod idle). Set by the fishing code (T-006); false until then. */
	UFUNCTION(BlueprintPure, Category="Lure|First Person")
	bool IsHoldingRod() const { return bHoldingRod; }

	UFUNCTION(BlueprintCallable, Category="Lure|First Person")
	void SetHoldingRod(bool bNewHoldingRod) { bHoldingRod = bNewHoldingRod; }

	/** The arms loop to play (DT_Movement RodPoseStill/RodPoseMoving by stance and motion; Idle without the rod). Owning client. */
	UFUNCTION(BlueprintPure, Category="Lure|First Person")
	EFPArmsPose GetArmsPose() const { return ArmsPose; }

	/** Crossfade time into the current pose (the row's RodPoseBlendTime), seconds. */
	UFUNCTION(BlueprintPure, Category="Lure|First Person")
	float GetArmsPoseBlendTime() const { return ArmsPoseBlendTime; }

	/** Plays the stance dip additive on the arms at PlayRate. False (and nothing happens) if the arms have no anim instance or the clip is missing. */
	UFUNCTION(BlueprintCallable, Category="Lure|First Person")
	bool PlayStanceDip(float PlayRate);

	/** The arms' current bob/sway offset relative to the camera. */
	UFUNCTION(BlueprintPure, Category="Lure|First Person")
	FTransform GetArmsBobOffset() const;

	// ---- Called by ULureCharacterMovementComponent ----

	/** Sets the replicated prone state (authority and the owning client's prediction). */
	void SetIsProne(bool bNewIsProne);

	virtual void OnStartProne();
	virtual void OnEndProne();

	/** The capsule changed size; OldFeetHeight is where the feet were (the camera stays put in the world). */
	void HandleCapsuleResized(float OldFeetHeight);

	/** Jump the camera straight to the current row's eye height (spawn, table swap). */
	void SnapEyeHeightToStance();

	/** Applies the replicated prone state on simulated proxies. */
	UFUNCTION()
	void OnRep_IsProne();

	// ---- ACharacter / APawn ----

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void NotifyControllerChanged() override;
	virtual void PawnClientRestart() override;
	virtual void Restart() override;
	virtual void OnRep_IsCrouched() override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void RecalculateBaseEyeHeight() override;
	virtual FVector GetPawnViewLocation() const override;
	virtual void Landed(const FHitResult& Hit) override;

protected:

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual bool CanJumpInternal_Implementation() const override;

	/** Prone posture, replicated to other players (COND_SimulatedOnly, like ACharacter::bIsCrouched). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_IsProne, Category="Lure|Movement")
	uint8 bIsProne : 1;

private:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	/** Arms seen only by this player; attached to the camera at a zero offset. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USkeletalMeshComponent> FirstPersonArms;

	/** Engine cylinder sized to the capsule; seen only by other players. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	/** Fishing (T-006): replicated, server-authoritative. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<ULureFishingComponent> Fishing;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlaceholderBodyMaterial;

	/** The loaded stance dip clip (null until imported). */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> LoadedStanceDip;

	bool bHoldingRod = false;

	/** Rod pose switch (T-006; client-side only, never replicated). */
	FLureRodPoseState RodPoseState;
	EFPArmsPose ArmsPose = EFPArmsPose::Idle;
	float ArmsPoseBlendTime = 0.3f;
	void UpdateArmsPose(const FLureMovementRow& Row, float DeltaSeconds);

	/** Bob/sway state (client-side only, never replicated). */
	FLureArmsBobState ArmsBobState;
	FRotator LastControlRotation = FRotator::ZeroRotator;
	bool bHasLastControlRotation = false;
	ELureStance LastDipStance = ELureStance::Stand;

	void UpdateArmsMotion(float DeltaSeconds);
	void LoadArmsAnimation();

	// Input handlers.
	void HandleMove(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleJumpPressed();
	void HandleJumpReleased();
	void HandleSprintPressed();
	void HandleSprintReleased();
	void HandleCrouchPressed();
	void HandleCrouchReleased();
	void HandlePronePressed();
	void HandleProneReleased();

	void AddMappingContextTo(APlayerController* PlayerController);
	void RemoveMappingContextFrom(APlayerController* PlayerController);

	void LoadFirstPersonArms();
	void SetupPlaceholderBodyMaterial();
	void UpdateEyeHeight(float DeltaSeconds);
	void BeginEyeBlend(float TargetEyeHeight, float Duration);
	void ClampEyeToHeadroom();
	void ApplyEyeHeight();
	void RefreshStanceVisuals();
	void UpdatePlaceholderBody();
	void ApplyBodyMeshOffset();
	void UpdateSprintToggle();

	float CurrentEyeHeight = 0.f;
	float EyeBlendFrom = 0.f;
	float EyeBlendTo = 0.f;
	float EyeBlendElapsed = 0.f;
	float EyeBlendDuration = 0.f;

	double LastMoveInputTime = 0.0;
	bool bSprintToggledOn = false;

	TWeakObjectPtr<APlayerController> MappedController;

public:

	/** The Interact key: sell points (T-010), later NPCs. */
	UFUNCTION(BlueprintPure, Category="Lure|Character")
	class ULureInteractionComponent* GetInteraction() const { return Interaction; }

private:

	/** Interact (T-010): finds the nearest interactable and asks the server to use it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<class ULureInteractionComponent> Interaction;
};

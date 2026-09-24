// Lure: fish anim instance (T-029). Parent class of ABP_Fish (graph only, no logic). Clips and rules: art/export/Fish/SK_Fish.anim.md.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "FishAnimInstance.generated.h"

class UAnimSequenceBase;

/**
 *  Which fish clip plays (ABP_Fish: Blend Poses by EFishAnimRole, one Sequence Player per role). Keep this order: the
 *  graph's pose pins follow it. The fight's move -> role mapping is data (DT_FishVisual MoveRoles).
 */
UENUM(BlueprintType)
enum class EFishAnimRole : uint8
{
	/** A_Fish_Swim_Idle: calm cruise. Fight moves Rest; a tired fish (slow, half alpha, on its side). */
	SwimIdle,
	/** A_Fish_Swim_Fast: fast swim. Fight moves Swim, Charge, unknown moves; escaping after a snap or thrown hook. */
	SwimFast,
	/** A_Fish_Hooked_Thrash: head shakes. The first HookSetThrashTime seconds after the hook set; the move Sulk. */
	Thrash,
	/** A_Fish_Fight_Run: all-out run. */
	Run,
	/** A_Fish_Fight_Dive: digging dive. */
	Dive,
	/** A_Fish_Fight_Dart: C-start dart; DartStartTime picks the left (0.0) or right (0.6 s) half. */
	Dart,
	/** A_Fish_Landed_Flop: out of the water. Always at alpha 1. */
	Flop,
	/**
	 *  A held pose, never a fight role (FFishVisualRow::Validate rejects it): the Sequence Player of this pin plays the
	 *  DisplayPose variable (its Sequence pin; A_Fish_Curled as the node's own clip) from DisplayPoseTime at play rate 0,
	 *  alpha 1. Set by SetHeldPose: the fish shown in an open cooler (DT_CoolerDisplay FishPose, T-030f).
	 */
	Curled
};

/** Everything ABP_Fish reads (the fight fish actor computes it; FFightFishVisual::ComputeAnimState). */
USTRUCT(BlueprintType)
struct FFishAnimState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish|Anim")
	EFishAnimRole Role = EFishAnimRole::SwimIdle;

	/** Sequence Player play rate (0.5..2 for swim roles, tail beat matched to speed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish|Anim")
	float PlayRate = 1.f;

	/** Apply Additive alpha, 0..1 (species AnimAmplitude; 1 for Flop). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish|Anim")
	float Amplitude = 1.f;

	/** Blend time of every Blend Poses pin, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish|Anim")
	float RoleBlendTime = 0.2f;

	/** Start Position of the Dart player: 0 = dart to the fish's left (-Y) first, 0.6 = to its right. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fish|Anim")
	float DartStartTime = 0.f;
};

/**
 *  C++ parent of ABP_Fish (skeleton SKEL_Fish; meshes SK_Bonefish, SK_CoralSnapper). The Blueprint child holds only this
 *  graph (SK_Fish.anim.md "ABP_Fish"), nothing else:
 *
 *    Local Space Ref Pose ------------------------------------------------> Apply Additive (Base)
 *    Blend Poses by EFishAnimRole (Active Enum Value <- Role) ---> Apply Additive (Additive), Alpha <- Amplitude
 *        SwimIdle -> Sequence Player A_Fish_Swim_Idle
 *        SwimFast -> Sequence Player A_Fish_Swim_Fast
 *        Thrash   -> Sequence Player A_Fish_Hooked_Thrash
 *        Run      -> Sequence Player A_Fish_Fight_Run
 *        Dive     -> Sequence Player A_Fish_Fight_Dive
 *        Dart     -> Sequence Player A_Fish_Fight_Dart   (Start Position <- DartStartTime)
 *        Flop     -> Sequence Player A_Fish_Landed_Flop
 *        Curled   -> Sequence Player A_Fish_Curled       (Sequence <- DisplayPose, Start Position <- DisplayPoseTime)
 *      every Sequence Player: Play Rate <- PlayRate, Loop on; every Blend Time pin <- RoleBlendTime; Blend Poses
 *      "Reset Child on Activation" ON (a role restarts from its start, so Thrash shakes first and Dart starts on its side).
 *    Apply Additive -> Output Pose.
 *
 *  The clips are Local Space additives on A_Fish_Rest frame 0 (don't use the skeleton's ref pose as the base; see the
 *  anim.md "Why additive"). No state machine, no slots, no event graph logic.
 *
 *  The values come from the owning ALureFightFish every update (UpdateFromOwner), or from SetAnimState (previews, other
 *  owners such as T-030's landed fish), or from SetHeldPose (a static pose; the owner no longer drives it).
 */
UCLASS(Transient, Blueprintable, BlueprintType)
class UFishAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:

	UPROPERTY(Transient, BlueprintReadOnly, Category="Lure|Fish")
	EFishAnimRole Role = EFishAnimRole::SwimIdle;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Lure|Fish")
	float PlayRate = 1.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Lure|Fish")
	float Amplitude = 1.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Lure|Fish")
	float RoleBlendTime = 0.2f;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Lure|Fish")
	float DartStartTime = 0.f;

	/** Sets every graph value (clamped: amplitude 0..1, play rate >= 0, blend time >= 0). */
	UFUNCTION(BlueprintCallable, Category="Lure|Fish")
	void SetAnimState(const FFishAnimState& State);

	UFUNCTION(BlueprintPure, Category="Lure|Fish")
	FFishAnimState GetAnimState() const;

	/** Copies the state of the owning ALureFightFish. False (nothing changed) when the owner is not one. */
	bool UpdateFromOwner();

	/** The clip the Curled pin plays (its Sequence Player's Sequence pin). Only meaningful while Role is Curled. */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Lure|Fish")
	TObjectPtr<UAnimSequenceBase> DisplayPose;

	/** Seconds into DisplayPose the fish holds (the Curled player's Start Position), >= 0 */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Lure|Fish")
	float DisplayPoseTime = 0.f;

	/**
	 *  Holds Pose (an additive A_Fish_* clip on SKEL_Fish) at Time from now on: Role Curled, DisplayPose, play rate 0,
	 *  alpha 1, no blend. The owner stops driving the state (UpdateFromOwner is skipped). A null Pose is ignored (false).
	 *  Check CanPlayRole(Curled) first: without that pin the graph would play its default pose instead.
	 */
	UFUNCTION(BlueprintCallable, Category="Lure|Fish")
	bool SetHeldPose(UAnimSequenceBase* Pose, float Time);

	/** True after SetHeldPose */
	bool HasHeldPose() const { return bHeldPose; }

	/** Whether this instance's graph has a Blend Poses pin of its own for InRole (ClassHasRolePin on its class). */
	virtual bool CanPlayRole(EFishAnimRole InRole) const;

	/**
	 *  Whether AnimClass (an Anim Blueprint class) has a Blend Poses by EFishAnimRole node with a pin for InRole. A role
	 *  without a pin plays the node's Default pin; SwimIdle (enum 0) always counts once the node exists. False for native
	 *  classes (no graph) and null.
	 */
	static bool ClassHasRolePin(const UClass* AnimClass, EFishAnimRole InRole);

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:

	/**
	 *  Sets the Curled pin's Sequence Player (the node its pose link points at) to DisplayPoseTime, so a re-pose shows
	 *  even when the role was already Curled (the player reads Start Position only when its pin activates). False
	 *  without a Curled pin wired straight to a sequence player (native class, no pin): nothing changes. Game thread,
	 *  before the graph update (NativeUpdateAnimation).
	 */
	bool SeekHeldPosePlayer();

	bool bHeldPose = false;

	/** SetHeldPose ran since the last update: SeekHeldPosePlayer on the next one */
	bool bHeldPoseSeekPending = false;
};

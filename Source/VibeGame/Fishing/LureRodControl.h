// Lure: rod steering during the reel fight (T-028). The owning client's side of it, as pure helpers (no world): the rod aim
// built from look input, its one-byte network form, the camera that follows the fish and the rod, the placeholder rod turn
// and the placeholder HUD words. ULureFishingComponent holds the state and does the networking; the fight's rules (what the
// rod does to the fish and the line) are in FishFight.h. Rules and numbers: docs/specs/reel-fight-rules.md "Rod steering".

#pragma once

#include "CoreMinimal.h"
#include "Fishing/FishFightTypes.h"

/**
 *  The owner's rod aim: look input accumulated since the fish was hooked, in look degrees, clamped to the rod's range
 *  (DT_FishFight RodAimUpDeg, RodAimDownDeg, RodAimSideDeg). The rod stays where the mouse leaves it (no spring back).
 */
struct FLureRodAim
{
	/** Degrees pulled back (+) or dipped (-) from level, and right (+) or left (-) of the line. */
	float PitchDeg = 0.f;
	float YawDeg = 0.f;

	/** Adds one frame of look input (the Look action: degrees, yaw right, pitch up) and clamps to the rod's range. */
	void AddLookInput(float YawDegrees, float PitchDegrees, const FLureFishFightRow& Tuning);

	/** Level and centered. */
	void Reset()
	{
		PitchDeg = 0.f;
		YawDeg = 0.f;
	}

	/** The aim as the fight reads it: pitch -1 (dipped) .. +1 (pulled back), yaw -1 (left) .. +1 (right). */
	float GetPitch01(const FLureFishFightRow& Tuning) const;
	float GetYaw01(const FLureFishFightRow& Tuning) const;
};

/** Rod steering helpers (pure, every machine). */
struct FLureRodControl
{
	/** One rod axis (-1..1) in a byte for ServerSetFightInput: 127 is exactly 0, 0 is -1, 254 is +1 (255 also reads +1). */
	static uint8 PackAxis(float Value01);
	static float UnpackAxis(uint8 Packed);

	/** Look degrees of a -1..1 aim (the inverse of FLureRodAim's normalization). */
	static float PitchDegrees(float Pitch01, const FLureFishFightRow& Tuning);
	static float YawDegrees(float Yaw01, const FLureFishFightRow& Tuning);

	/**
	 *  Where the owner's camera heads while a fish is on: straight at the fish from the eye, turned by CameraRodYawShare /
	 *  CameraRodPitchShare of the rod's aim (in look degrees). Pitch within [-80, 80].
	 */
	static FRotator CameraTarget(const FVector& Eye, const FVector& Fish, float Pitch01, float Yaw01, const FLureFishFightRow& Tuning);

	/** One eased camera step from Current toward Target (time constant CameraFollowTime; yaw the short way round; no roll). */
	static FRotator CameraStep(const FRotator& Current, const FRotator& Target, float DeltaTime, const FLureFishFightRow& Tuning);

	/** An eased aim (for the arms' aim offset and the other players' rod): time constant TimeConstant, s (0 = no easing). */
	static FVector2D EaseAim(const FVector2D& Current, const FVector2D& Target, float DeltaTime, float TimeConstant);

	/** The rod's placeholder turn on its grip bone at an aim (+pitch = tip up, +yaw = tip right), degrees (RodAimLook*Deg). */
	static FRotator RodLook(float Pitch01, float Yaw01, const FLureFishFightRow& Tuning);

	/** HUD words for the rod angle: "level", "back", "dipped", with "-left" / "-right" (zones at one third of the range). */
	static FString DescribeRod(float Pitch01, float Yaw01);

	/** HUD: "Reel 2/3 (wheel or LB/RB)" for the 0-based Step of NumSteps. */
	static FString ReelText(int32 Step, int32 NumSteps);

	/** HUD: "Fish runs LEFT: pull right" / "Fish runs RIGHT: pull left"; empty when there is no side. */
	static FString RunHint(ELureFightRunSide Side);

	/** Run direction (-1 left, 0 none, +1 right) <-> the replicated ELureFightRunSide. */
	static ELureFightRunSide RunSideFromDirection(int32 RunDir);
	static int32 DirectionFromRunSide(ELureFightRunSide Side);
};

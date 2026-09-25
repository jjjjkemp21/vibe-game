// Lure: things the player can use with the two use keys (T-010 interact key; T-030 verbs: fish, coolers, the sell counter;
// later NPCs, boats...). Rules: docs/specs/catch-handling-rules.md "Focus and input".

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "LureInteractable.generated.h"

class APawn;

/** The two use keys: Interact (E / gamepad X) and AltInteract (F / gamepad Y). */
UENUM(BlueprintType)
enum class ELureInteractKey : uint8
{
	Primary,
	Secondary
};

/**
 *  Everything a use key can do. The client sends the verb it expects; the server does it only if its own verb for that key
 *  is the same (a stale prompt never does something else). A new kind of interactable adds its verbs at the end.
 */
UENUM(BlueprintType)
enum class ELureInteractVerb : uint8
{
	None,
	/** A fish on your hook, or lying loose: into your hand */
	GrabFish,
	/** Your hanging fish: let it drop off the hook (into the water = released) */
	ReleaseFish,
	/** The fish in your hand: drop it in front of you (into the water = released) */
	DropFish,
	OpenCooler,
	CloseCooler,
	PutFishInCooler,
	TakeFishFromCooler,
	PickUpCooler,
	PutDownCooler,
	PlaceFishOnCounter,
	TakeFishFromCounter,
	SellCounter,
	/** T-064: the cooler you carry, open: turn its open side away from you ("Show the fish") */
	ShowCooler,
	/** T-064: the cooler you show: turn its open side back toward you ("Turn it back") */
	TurnBackCooler,
	/** T-065: the cooler you show, with fish in it: tip every fish out in front of you ("Dump 3 fish") */
	DumpCooler
};

/** What a key would do on an interactable right now. Verb None with a prompt is an info line (e.g. "Cooler full (4/4)"). */
USTRUCT(BlueprintType)
struct FLureInteraction
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Interaction")
	ELureInteractVerb Verb = ELureInteractVerb::None;

	UPROPERTY(BlueprintReadOnly, Category="Interaction")
	FText Prompt;

	bool HasVerb() const { return Verb != ELureInteractVerb::None; }
	bool IsEmpty() const { return !HasVerb() && Prompt.IsEmpty(); }

	static FLureInteraction Make(ELureInteractVerb InVerb, const FText& InPrompt)
	{
		FLureInteraction Result;
		Result.Verb = InVerb;
		Result.Prompt = InPrompt;
		return Result;
	}

	static FLureInteraction Info(const FText& InPrompt)
	{
		FLureInteraction Result;
		Result.Prompt = InPrompt;
		return Result;
	}
};

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class ULureInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 *  C++ interface for an interactable actor. The actor registers itself with ULureInteractionSubsystem (BeginPlay) and
 *  unregisters (EndPlay). ULureInteractionComponent on the player's pawn picks what the player looks at (in reach, smallest
 *  focus angle), shows the verbs of both keys, and on a key asks the server to do the verb (checked again there).
 */
class ILureInteractable
{
	GENERATED_BODY()

public:

	/** World point the reach is measured from */
	virtual FVector GetInteractionLocation() const = 0;

	/** Reach in cm (3D distance from the pawn's location to GetInteractionLocation) */
	virtual float GetInteractionRadius() const = 0;

	/** Size of the target for aiming, cm: the default GetFocusAngle treats it as a sphere of this radius around the location */
	virtual float GetFocusRadius() const { return 0.0f; }

	/** Degrees between the view ray and the target (0 = looking right at it). Default: a sphere of GetFocusRadius. */
	virtual float GetFocusAngle(const FVector& ViewLocation, const FVector& ViewDirection) const;

	/**
	 *  T-030g: how far along the view ray (cm from ViewLocation) the ray enters this target's shape; negative if it misses.
	 *  A target the ray hits wins the focus over any it only comes near (the nearest hit first). Default: the focus sphere
	 *  (GetFocusRadius around GetInteractionLocation; radius 0 = never hit, the angle rule only).
	 */
	virtual double GetFocusHitDistance(const FVector& ViewLocation, const FVector& ViewDirection) const;

	/** Checks besides reach (default: any pawn). False = no verbs and no focus for this pawn. */
	virtual bool CanInteract(const APawn* Pawn) const { return Pawn != nullptr; }

	/** What Key does for Pawn now (Verb None = nothing; a prompt without a verb is shown as info). Every machine. */
	virtual FLureInteraction GetInteraction(const APawn* Pawn, ELureInteractKey Key) const = 0;

	/**
	 *  T-030h: a fingerprint of what Verb would act on now, for verbs whose prompt shows contents that can change under it
	 *  (the counter's "Sell N fish (X coins)": the fish on it). Every machine computes it from its own replicated view, the
	 *  same way. The client sends the one it saw with the key; the server refuses when its own differs (a race changed the
	 *  contents), so a verb never does more or less than the prompt showed. 0 = the verb has nothing like that (not checked).
	 */
	virtual int32 GetInteractionStateToken(const APawn* Pawn, ELureInteractVerb Verb) const { return 0; }

	/** Server only: does Verb for Pawn. ULureInteractionComponent::TryInteract has checked reach, CanInteract, the verb and its state token. */
	virtual bool PerformInteraction(APawn* Pawn, ELureInteractVerb Verb) = 0;

	/** Extra reach (cm) the server accepts, for network lag between the client's check and the server's */
	static constexpr float ServerRangeSlack = 150.0f;

	/** Distance from Pawn to GetInteractionLocation() <= GetInteractionRadius() + Slack */
	bool IsInInteractionRange(const APawn* Pawn, float Slack = 0.0f) const;

	/** Degrees from the ray (ViewLocation, ViewDirection) to a sphere: 0 when the ray passes through it (or starts inside). */
	static float AngleToSphere(const FVector& ViewLocation, const FVector& ViewDirection, const FVector& Center, float Radius);

	/** Distance along the ray (ViewLocation, ViewDirection) to where it enters a sphere: 0 from inside, negative on a miss (or radius <= 0) */
	static double RayToSphere(const FVector& ViewLocation, const FVector& ViewDirection, const FVector& Center, float Radius);

	/** Distance along the ray to where it enters an oriented box (HalfExtent in BoxTransform's space, scale ignored): 0 from inside, negative on a miss */
	static double RayToBox(const FVector& ViewLocation, const FVector& ViewDirection, const FTransform& BoxTransform, const FVector& HalfExtent);

	/** Degrees from the ray to an oriented box (HalfExtent in BoxTransform's space, scale ignored): 0 when the ray passes through it. */
	static float AngleToBox(const FVector& ViewLocation, const FVector& ViewDirection, const FTransform& BoxTransform, const FVector& HalfExtent);

	/** The key's display label for prompts: "E", "F" (the first key of the action), or "Interact" / "Alt" without keys. */
	static FString GetKeyLabel(ELureInteractKey Key);
};

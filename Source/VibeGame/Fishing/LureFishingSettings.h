// Lure: fishing settings (T-006). Project Settings > Game > Lure Fishing; stored in Config/DefaultGame.ini.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "InputCoreTypes.h"
#include "Fishing/FishFightTypes.h"
#include "LureFishingSettings.generated.h"

class UAnimMontage;
class UDataTable;
class UMaterialInterface;
class USoundBase;
class UStaticMesh;

/**
 *  Fishing settings in [/Script/VibeGame.LureFishingSettings] (DefaultGame.ini). Gameplay tuning (cast, bite, hook, bobber,
 *  line) is the DT_Fishing table this points at; here are the asset references, the world rules (water, spots) and keys.
 *  Every asset is optional at runtime: a missing one is logged once and that visual (or sound) is skipped.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Lure Fishing"))
class ULureFishingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	ULureFishingSettings();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ---- Data ----

	/** Fishing tuning table (row struct LureFishingRow; source data/tables/DT_Fishing.csv). Missing = built-in profile + one warning. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Data", meta=(RequiredAssetDataTags="RowStructure=/Script/VibeGame.LureFishingRow"))
	TSoftObjectPtr<UDataTable> FishingTable;

	/** DT_Fishing row a player uses unless the fishing component names another (gear profiles later). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Data")
	FName DefaultProfileRow;

	/** How the physics fishing line moves (row struct LureFishingLineRow; source data/tables/DT_FishingLine.csv; T-032). Missing = built-in tuning, logged once. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Data", meta=(RequiredAssetDataTags="RowStructure=/Script/VibeGame.LureFishingLineRow"))
	TSoftObjectPtr<UDataTable> FishingLineTable;

	/** DT_FishingLine row the line uses (a line gear item can name its own later). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Data")
	FName FishingLineRow;

	// ---- Assets ----

	/** Rod held by the first-person arms (attached to RodAttachBone, world scale kept). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	TSoftObjectPtr<UStaticMesh> RodMesh;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	TSoftObjectPtr<UStaticMesh> BobberMesh;

	/** A thin mesh along Z (the engine cylinder) bent along the line by spline mesh segments. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	TSoftObjectPtr<UStaticMesh> LineMesh;

	/** Line material; its vector parameter "Color" gets LineColor (the engine basic shape material has one). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	TSoftObjectPtr<UMaterialInterface> LineMaterial;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	FLinearColor LineColor;

	/** Sounds (optional; none = silent). The bite sound plays at the bobber. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	TSoftObjectPtr<USoundBase> BiteSound;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	TSoftObjectPtr<USoundBase> NibbleSound;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	TSoftObjectPtr<USoundBase> CastSound;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	TSoftObjectPtr<USoundBase> HookSound;

	/** Arms montages (animation-artist, later). Set = played on the arms instead of the procedural rod swing. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	TSoftObjectPtr<UAnimMontage> CastMontage;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	TSoftObjectPtr<UAnimMontage> HookMontage;

	/** Bone of SK_FPArms the rod attaches to (its frame is the rod's pivot frame; the prone tuck animates it). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	FName RodAttachBone;

	/** Rod socket the line leaves from. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	FName RodLineSocket;

	/** Bobber socket the line attaches to. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	FName BobberLineSocket;

	/** Where the line starts for OTHER players' characters (they have no visible rod yet): offset from their eye, view space (X fwd, Y right, Z up), cm. Also the server's cast origin. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets")
	FVector RodTipOffsetFromEye;

	/** Screen width the line pixel width refers to (1080p). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Assets", meta=(ClampMin="1"))
	float LineReferenceScreenWidth = 1920.f;

	// ---- World ----

	/** Actor tag of fishing spot markers (L_PalmKey.md section 11). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="World")
	FName FishingSpotTag;

	/** Actor tag of water surfaces (the top of the actor's bounds is the surface). Water physics volumes count too. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="World")
	FName WaterTag;

	/** With no water volume or tagged water under the bobber, the sea is at FallbackWaterZ (the layouts' water_z). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="World")
	bool bUseFallbackWaterZ = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="World")
	float FallbackWaterZ = 0.f;

	/** Ground more than this above the water surface counts as land (the bobber lies there; nothing bites), cm. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="World", meta=(ClampMin="0"))
	float LandTolerance = 2.f;

	/** Habitat (gameplay tag name) used when the bobber is in no fishing spot. None (default) = nothing bites off-spot. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="World")
	FName OffSpotHabitat;

	/** Region (gameplay tag name) used when there is no spot or the spot has no Region= tag. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="World")
	FName DefaultRegion;

	/**
	 *  Bait on every hook until gear exists (T-011), a gameplay tag name. Species with an AcceptedBait list only bite on a
	 *  matching bait; Bait.Shrimp suits both starter species. None = no bait (only species with an empty AcceptedBait bite).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="World")
	FName DefaultBait;

	/** Time of day for bites until the day/night cycle exists (T-013), hours. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="World", meta=(ClampMin="0", ClampMax="24"))
	float DefaultTimeOfDayHours = 12.f;

	/** Players start with the rod in hand (no inventory yet). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="World")
	bool bRodInHandByDefault = true;

	// ---- Controls ----

	/** The fishing button: hold and release to cast; press while the line is out to hook (or reel in before a bite). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls")
	TArray<FKey> CastKeys;

	/** Optional separate hook button (none by default: the cast button hooks). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls")
	TArray<FKey> HookKeys;

	/** T-028: one reel speed step faster / slower while a fish is on (mouse wheel up / down, right / left bumper). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls")
	TArray<FKey> ReelFasterKeys;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls")
	TArray<FKey> ReelSlowerKeys;

	/**
	 *  T-028: while a fish is on, the owner sends its rod aim and reel step to the server (unreliable) at most this often,
	 *  seconds, and at least every FightInputResendSeconds even when nothing changed (so a lost packet is repaired).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls", meta=(ClampMin="0.01"))
	float FightInputSendSeconds = 0.05f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls", meta=(ClampMin="0.05"))
	float FightInputResendSeconds = 0.25f;

	/** Seconds a result or a refusal stays in the placeholder HUD text. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Controls", meta=(ClampMin="0"))
	float HudMessageSeconds = 2.5f;

	// ---- Reel fight and gear (T-007; docs/specs/reel-fight-rules.md) ----

	/** Rods, lines and hooks/bait (row struct LureGearRow; source data/tables/DT_Gear.csv). Missing = built-in starter items + one warning. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Fight", meta=(RequiredAssetDataTags="RowStructure=/Script/VibeGame.LureGearRow"))
	TSoftObjectPtr<UDataTable> GearTable;

	/** Fish fight patterns (row struct LureFightPatternRow; source data/tables/DT_FightPattern.json). Row name = a species' FightPatternId. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Fight", meta=(RequiredAssetDataTags="RowStructure=/Script/VibeGame.LureFightPatternRow"))
	TSoftObjectPtr<UDataTable> FightPatternTable;

	/** Fight tuning (row struct LureFishFightRow; source data/tables/DT_FishFight.csv). Missing = built-in tuning + one warning. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Fight", meta=(RequiredAssetDataTags="RowStructure=/Script/VibeGame.LureFishFightRow"))
	TSoftObjectPtr<UDataTable> FishFightTable;

	/** DT_FishFight row used. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Fight")
	FName FishFightRow;

	/** Gear every player starts with (DT_Gear row names) until the shop (T-012) and saves change it. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Fight")
	FLureGearLoadout DefaultLoadout;

	/** Arms montages for the fight (animation-artist, later; none = the procedural rod bend only): looped while reeling, played when a fish is hooked and when it is landed. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Fight")
	TSoftObjectPtr<UAnimMontage> ReelMontage;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Fight")
	TSoftObjectPtr<UAnimMontage> FightMontage;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Fight")
	TSoftObjectPtr<UAnimMontage> LandMontage;
};

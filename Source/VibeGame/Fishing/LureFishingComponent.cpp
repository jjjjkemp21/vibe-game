// Lure: casting, bobber, bite and hook (T-006); reel fight, line tension and gear (T-007).

#include "Fishing/LureFishingComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Catch/LureCatchLibrary.h"
#include "Catch/LureHandsComponent.h"
#include "Character/LureCharacterMovementComponent.h"
#include "Character/LureInputSubsystem.h"
#include "Character/LurePlayerCharacter.h"
#include "CollisionQueryParams.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "Environment/LureDayClockComponent.h"
#include "Fish/FishRoll.h"
#include "Fish/FishSettings.h"
#include "Fishing/FishingSpots.h"
#include "Fishing/FishingWater.h"
#include "Fishing/LureFishingLineComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Net/UnrealNetwork.h"
#include "Progression/LureProgressionLibrary.h"
#include "Sound/SoundBase.h"

namespace LureFishingPrivate
{
	/** Loads a soft reference if its package exists (no load errors for assets not imported yet, e.g. in lanes). */
	template <typename T>
	T* LoadIfExists(const TSoftObjectPtr<T>& Ref)
	{
		if (Ref.IsNull())
		{
			return nullptr;
		}
		if (T* Loaded = Ref.Get())
		{
			return Loaded;
		}
		const FString Package = Ref.ToSoftObjectPath().GetLongPackageName();
		if (Package.IsEmpty() || !FPackageName::DoesPackageExist(Package))
		{
			return nullptr;
		}
		return Ref.LoadSynchronous();
	}

	const TCHAR* ReasonText(ELureCastBlock Reason)
	{
		switch (Reason)
		{
		case ELureCastBlock::NoRod: return TEXT("no rod in hand");
		case ELureCastBlock::Swimming: return TEXT("swimming");
		case ELureCastBlock::Sprinting: return TEXT("sprinting");
		case ELureCastBlock::InAir: return TEXT("in the air");
		case ELureCastBlock::RodTucked: return TEXT("crawling");
		case ELureCastBlock::Busy: return TEXT("the line is already out");
		case ELureCastBlock::TooFar: return TEXT("too far from the bobber");
		case ELureCastBlock::Climbing: return TEXT("climbing");
		case ELureCastBlock::Teleported: return TEXT("teleported");
		case ELureCastBlock::Unpossessed: return TEXT("the player left");
		case ELureCastBlock::None:
		default: return TEXT("");
		}
	}

	FString FishLabel(const FFishInstance& Fish)
	{
		if (!Fish.IsValid())
		{
			return TEXT("a fish");
		}
		FString Label = FString::Printf(TEXT("%s (%s), %.2f kg, %d coins"), *Fish.SpeciesId.ToString(), *Fish.RarityId.ToString(), Fish.WeightKg, Fish.Value);
		if (Fish.ModifierIds.Num() > 0)
		{
			TArray<FString> Mods;
			for (const FName& Mod : Fish.ModifierIds)
			{
				Mods.Add(Mod.ToString());
			}
			Label += TEXT(" [") + FString::Join(Mods, TEXT(", ")) + TEXT("]");
		}
		return Label;
	}

	/** One warning per session for a missing fishing table (every character resolves it). */
	bool bWarnedFallbackProfile = false;

	/** One warning per session per missing fight table (T-007). */
	bool bWarnedGearTable = false;
	bool bWarnedPatternTable = false;
	bool bWarnedFightTable = false;

	/** Loads a settings table if its asset exists; warns once (bWarned) when it is missing. */
	UDataTable* LoadFightTable(const TSoftObjectPtr<UDataTable>& Ref, const TCHAR* Source, const TCHAR* Using, bool& bWarned)
	{
		UDataTable* Table = LoadIfExists(Ref);
		if (!Table && !bWarned)
		{
			bWarned = true;
			UE_LOG(LogLureFishing, Warning, TEXT("Fight: '%s' is not imported (source data/tables/%s); using %s."), *Ref.ToString(), Source, Using);
		}
		return Table;
	}

	/** Plain-text tension bar for the placeholder HUD: 24 cells for 0..120 % of the line's strength, '|' at 100 %. */
	FString TensionBar(float Tension01)
	{
		FString Bar = TEXT("[");
		const float Value = FMath::IsFinite(Tension01) ? FMath::Max(0.f, Tension01) : 0.f;
		for (int32 Cell = 0; Cell < 24; ++Cell)
		{
			if (Cell == 20)
			{
				Bar += TEXT("|");
			}
			Bar += (static_cast<float>(Cell) + 0.5f) * 0.05f <= Value ? TEXT("#") : TEXT(".");
		}
		return Bar + TEXT("]");
	}

	const TCHAR* OutcomeText(ELureFightOutcome Outcome)
	{
		switch (Outcome)
		{
		case ELureFightOutcome::Landed: return TEXT("landed");
		case ELureFightOutcome::Snapped: return TEXT("the line snapped");
		case ELureFightOutcome::Spooled: return TEXT("the fish took all the line");
		case ELureFightOutcome::ThrewHook: return TEXT("the fish threw the hook");
		default: return TEXT("");
		}
	}
}

ULureFishingComponent::ULureFishingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After animation and movement: the rod socket and the camera are final for this frame when the line is drawn.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetIsReplicatedByDefault(true);
}

void ULureFishingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ULureFishingComponent, NetState);
	DOREPLIFETIME(ULureFishingComponent, HookedFish);
	DOREPLIFETIME(ULureFishingComponent, LastLandedFish);
	DOREPLIFETIME(ULureFishingComponent, Loadout);
	DOREPLIFETIME(ULureFishingComponent, FightNet);
}

void ULureFishingComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!bSeedSet)
	{
		Rng.Initialize(FFishRoll::MakeRandomSeed());
		bSeedSet = true;
	}
	if (!GetDefault<ULureFishingSettings>()->bRodInHandByDefault)
	{
		bRodEquipped = false; // project-wide: players start without the rod
	}
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		Loadout = GetEffectiveLoadout(); // the default gear, replicated by name (the shop and saves change it later)
		bGearResolved = false;
	}
}

// ---- Profile ----

void ULureFishingComponent::ResolveProfile()
{
	bProfileResolved = true;
	const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
	const UDataTable* Table = LureFishingPrivate::LoadIfExists(Settings->FishingTable);
	ApplyProfileFrom(Table, Table ? FString() : FString::Printf(TEXT("'%s' is not imported (source data/tables/DT_Fishing.csv)"), *Settings->FishingTable.ToString()));
}

void ULureFishingComponent::ApplyFishingTable(const UDataTable* Table)
{
	bProfileResolved = true;
	ApplyProfileFrom(Table, TEXT("no table"));
}

void ULureFishingComponent::ApplyProfileFrom(const UDataTable* Table, const FString& MissingReason)
{
	const FName RowName = ProfileRow.IsNone() ? GetDefault<ULureFishingSettings>()->DefaultProfileRow : ProfileRow;
	FString Problem;
	const FLureFishingRow* Row = nullptr;
	if (!Table)
	{
		Problem = MissingReason;
	}
	else if (!Table->GetRowStruct() || !Table->GetRowStruct()->IsChildOf(FLureFishingRow::StaticStruct()))
	{
		Problem = FString::Printf(TEXT("%s has row struct '%s', expected LureFishingRow"), *Table->GetName(), Table->GetRowStruct() ? *Table->GetRowStruct()->GetName() : TEXT("none"));
	}
	else
	{
		// FindRowUnchecked: the struct was checked above, and it never logs (a missing row is reported below as one warning).
		Row = reinterpret_cast<const FLureFishingRow*>(Table->FindRowUnchecked(RowName));
		if (!Row)
		{
			Problem = FString::Printf(TEXT("%s has no row '%s'"), *Table->GetName(), *RowName.ToString());
		}
		else if (!Row->Validate(Problem))
		{
			Problem = FString::Printf(TEXT("%s row '%s' is invalid (%s)"), *Table->GetName(), *RowName.ToString(), *Problem);
			Row = nullptr;
		}
	}

	if (Row)
	{
		Profile = *Row;
		bFallbackProfile = false;
		return;
	}
	Profile = FLureFishingRules::GetFallbackRow();
	bFallbackProfile = true;
	if (!LureFishingPrivate::bWarnedFallbackProfile)
	{
		LureFishingPrivate::bWarnedFallbackProfile = true;
		UE_LOG(LogLureFishing, Warning, TEXT("Fishing table: %s; %s."), *Problem, FLureFishingRules::FallbackWarningMarker);
	}
	else
	{
		UE_LOG(LogLureFishing, Verbose, TEXT("Fishing table: %s; %s."), *Problem, FLureFishingRules::FallbackWarningMarker);
	}
}

void ULureFishingComponent::SetFishingProfile(const FLureFishingRow& Row)
{
	bProfileResolved = true;
	FString Problem;
	if (Row.Validate(Problem))
	{
		Profile = Row;
		bFallbackProfile = false;
	}
	else
	{
		UE_LOG(LogLureFishing, Warning, TEXT("SetFishingProfile: invalid row (%s); %s."), *Problem, FLureFishingRules::FallbackWarningMarker);
		Profile = FLureFishingRules::GetFallbackRow();
		bFallbackProfile = true;
	}
}

const FLureFishingRow& ULureFishingComponent::GetProfile() const
{
	if (!bProfileResolved)
	{
		const_cast<ULureFishingComponent*>(this)->ResolveProfile();
	}
	return Profile;
}

bool ULureFishingComponent::IsUsingFallbackProfile() const
{
	GetProfile();
	return bFallbackProfile;
}

// ---- Fight tables and gear (T-007) ----

void ULureFishingComponent::ResolveFightTables()
{
	if (bFightTablesResolved)
	{
		return;
	}
	using namespace LureFishingPrivate;
	const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
	GearTableRef = LoadFightTable(Settings->GearTable, TEXT("DT_Gear.csv"), TEXT("the built-in starter gear"), bWarnedGearTable);
	PatternTableRef = LoadFightTable(Settings->FightPatternTable, TEXT("DT_FightPattern.json"), TEXT("the built-in fight pattern"), bWarnedPatternTable);
	FightTableRef = LoadFightTable(Settings->FishFightTable, TEXT("DT_FishFight.csv"), TEXT("the built-in fight tuning"), bWarnedFightTable);
	SetFightTables(GearTableRef, PatternTableRef, FightTableRef);
}

void ULureFishingComponent::SetFightTables(const UDataTable* InGearTable, const UDataTable* InPatternTable, const UDataTable* InFightTable)
{
	bFightTablesResolved = true;
	bGearResolved = false;
	GearTableRef = const_cast<UDataTable*>(InGearTable);
	PatternTableRef = const_cast<UDataTable*>(InPatternTable);
	FightTableRef = const_cast<UDataTable*>(InFightTable);

	FightTuning = FLureFishFightRow::GetFallbackRow();
	if (!InFightTable)
	{
		return;
	}
	const FName RowName = GetDefault<ULureFishingSettings>()->FishFightRow;
	FString Problem;
	const FLureFishFightRow* Row = nullptr;
	if (InFightTable->GetRowStruct() && InFightTable->GetRowStruct()->IsChildOf(FLureFishFightRow::StaticStruct()))
	{
		Row = reinterpret_cast<const FLureFishFightRow*>(InFightTable->FindRowUnchecked(RowName));
	}
	if (Row && Row->Validate(Problem))
	{
		FightTuning = *Row;
		return;
	}
	UE_LOG(LogLureFishing, Warning, TEXT("Fight: %s row '%s' is missing or invalid (%s); using the built-in fight tuning."),
		*InFightTable->GetName(), *RowName.ToString(), Problem.IsEmpty() ? TEXT("missing row or wrong row struct") : *Problem);
}

const FLureFishFightRow& ULureFishingComponent::GetFightTuning() const
{
	const_cast<ULureFishingComponent*>(this)->ResolveFightTables();
	return FightTuning;
}

FLureGearLoadout ULureFishingComponent::GetEffectiveLoadout() const
{
	const FLureGearLoadout& Defaults = GetDefault<ULureFishingSettings>()->DefaultLoadout;
	FLureGearLoadout Effective = Loadout;
	for (const ELureGearSlot Slot : { ELureGearSlot::Rod, ELureGearSlot::Line, ELureGearSlot::Hook })
	{
		if (Effective.Get(Slot).IsNone())
		{
			Effective.Set(Slot, Defaults.Get(Slot));
		}
	}
	return Effective;
}

FLureGearLoadout ULureFishingComponent::GetLoadout() const
{
	return GetEffectiveLoadout();
}

FLureGearStats ULureFishingComponent::GetGearStats() const
{
	if (!bGearResolved)
	{
		const_cast<ULureFishingComponent*>(this)->ResolveFightTables();
		TArray<FString> Problems;
		GearStats = FLureGear::Resolve(GearTableRef, GetEffectiveLoadout(), &Problems);
		FLureGear::ApplyDragLineCap(GearStats, FightTuning.DragLineCap);
		bGearResolved = true;
		if (GearTableRef && Problems.Num() > 0)
		{
			UE_LOG(LogLureFishing, Warning, TEXT("%s: gear %s uses built-in items (%s)."), *GetNameSafe(GetOwner()), *GearTableRef->GetName(), *FString::Join(Problems, TEXT("; ")));
		}
	}
	return GearStats;
}

bool ULureFishingComponent::AuthoritySetLoadout(const FLureGearLoadout& NewLoadout)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || NetState.State == ELureFishingState::Hooked)
	{
		return false;
	}
	ResolveFightTables();
	bool bAllAccepted = true;
	FLureGearLoadout Next = GetEffectiveLoadout();
	for (const ELureGearSlot Slot : { ELureGearSlot::Rod, ELureGearSlot::Line, ELureGearSlot::Hook })
	{
		const FName Id = NewLoadout.Get(Slot);
		if (Id.IsNone())
		{
			continue;
		}
		FString Problem;
		if (FLureGear::CanEquip(GearTableRef, Slot, Id, &Problem))
		{
			Next.Set(Slot, Id);
		}
		else
		{
			bAllAccepted = false;
			UE_LOG(LogLureFishing, Warning, TEXT("%s: can't equip %s (%s)."), *GetNameSafe(GetOwner()), *Id.ToString(), GearTableRef ? *Problem : TEXT("DT_Gear is not imported"));
		}
	}
	Loadout = Next;
	bGearResolved = false;
	return bAllAccepted;
}

void ULureFishingComponent::OnRep_Loadout()
{
	bGearResolved = false;
}

FLureFightPatternRow ULureFishingComponent::FindPattern(FName PatternId, FName& OutUsedId)
{
	ResolveFightTables();
	const FLureFightPatternRow* Row = nullptr;
	if (PatternTableRef && !PatternId.IsNone() && PatternTableRef->GetRowStruct() && PatternTableRef->GetRowStruct()->IsChildOf(FLureFightPatternRow::StaticStruct()))
	{
		Row = reinterpret_cast<const FLureFightPatternRow*>(PatternTableRef->FindRowUnchecked(PatternId));
	}
	FString Problem;
	if (Row && Row->Validate(Problem))
	{
		OutUsedId = PatternId;
		return *Row;
	}
	if (PatternTableRef)
	{
		UE_LOG(LogLureFishing, Warning, TEXT("Fight: fight pattern '%s' is %s in %s; using the built-in pattern."), *PatternId.ToString(),
			Row ? *FString::Printf(TEXT("invalid (%s)"), *Problem) : TEXT("missing"), *PatternTableRef->GetName());
	}
	OutUsedId = NAME_None;
	return FLureFightPatternRow::GetFallbackPattern();
}

// ---- Owner queries ----

ACharacter* ULureFishingComponent::GetCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

APawn* ULureFishingComponent::GetPawn() const
{
	return Cast<APawn>(GetOwner());
}

bool ULureFishingComponent::IsOwnerLocallyControlled() const
{
	const APawn* Pawn = GetPawn();
	return Pawn && Pawn->IsLocallyControlled();
}

UCameraComponent* ULureFishingComponent::GetOwnerCamera() const
{
	if (const ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(GetOwner()))
	{
		return Lure->GetFirstPersonCamera();
	}
	return GetOwner() ? GetOwner()->FindComponentByClass<UCameraComponent>() : nullptr;
}

FVector ULureFishingComponent::GetEyeLocation() const
{
	const APawn* Pawn = GetPawn();
	if (Pawn)
	{
		return Pawn->GetPawnViewLocation();
	}
	return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
}

const FLureMovementRow& ULureFishingComponent::GetMovementRow() const
{
	if (const ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(GetOwner()))
	{
		if (const ULureCharacterMovementComponent* Movement = Lure->GetLureMovement())
		{
			return Movement->GetRow(Movement->GetMovementState());
		}
	}
	static const FLureMovementRow Stand = FLureMovementData::GetFallbackRow(ELureMovementState::Stand);
	return Stand;
}

FLureCastConditions ULureFishingComponent::GetConditions() const
{
	FLureCastConditions Conditions;
	const ACharacter* Character = GetCharacter();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	// T-030: the rod is stowed while the hands hold a fish or the cooler (a line out comes in, reason NoRod).
	Conditions.bHasRod = bRodEquipped && !ULureHandsComponent::IsRodStowedFor(GetOwner());
	Conditions.bSwimming = IsInWater();
	Conditions.bFalling = Movement && Movement->IsFalling();
	// T-026 climbs (ClimbOut, LedgeClimb) are MOVE_Custom: busy, like any custom mode added later.
	Conditions.bClimbing = Movement && Movement->MovementMode == MOVE_Custom;
	Conditions.Speed2D = Movement ? static_cast<float>(Movement->Velocity.Size2D()) : 0.f;
	// T-030: a landed fish still hangs on the line until it is grabbed or let go (a cast is refused as Busy).
	Conditions.bLineOut = IsLineOut() || ULureHandsComponent::HasFishOnHookFor(GetOwner());
	return Conditions;
}

bool ULureFishingComponent::IsInWater() const
{
	// T-026: the player character is in the water from falling in until it stands on land again (the climb out included).
	if (const ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(GetOwner()))
	{
		return Lure->IsSwimming();
	}
	const ACharacter* Character = GetCharacter();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	return Movement && Movement->IsSwimming();
}

bool ULureFishingComponent::IsRodInHand() const
{
	// The rod is put away in the water (the arms show Idle); it comes back once you stand on land.
	// T-030: it is also put away while the hands hold a fish or carry the cooler.
	return bRodEquipped && !IsInWater() && !ULureHandsComponent::IsRodStowedFor(GetOwner());
}

ELureCastBlock ULureFishingComponent::GetCastBlock() const
{
	ELureCastBlock Block = FLureFishingRules::GetCastBlock(GetConditions(), GetMovementRow());
	if (Block == ELureCastBlock::None && IsOwnerLocallyControlled())
	{
		// The owner's arms may still show the tuck (RodStillDelay after crawling, or a wall right ahead while prone).
		if (const ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(GetOwner()))
		{
			if (FLureRodPose::IsTucked(Lure->GetArmsPose()))
			{
				Block = ELureCastBlock::RodTucked;
			}
		}
	}
	return Block;
}

double ULureFishingComponent::GetFishingTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

double ULureFishingComponent::GetLocalTime() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

float ULureFishingComponent::GetHookGrace() const
{
	const APawn* Pawn = GetPawn();
	const bool bRemotePlayer = GetNetMode() != NM_Standalone && Pawn && !Pawn->IsLocallyControlled();
	return bRemotePlayer ? GetProfile().HookLatencyGrace : 0.f;
}

// ---- Input (owning client) ----

void ULureFishingComponent::BindInput(UEnhancedInputComponent& Input)
{
	BindRodInput(Input); // T-028: reel speed steps (the look input reaches the rod through ALurePlayerCharacter::DoLook)
	UInputAction* CastAction = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::Cast);
	UInputAction* HookAction = ULureInputSubsystem::GetInputActionByName(FLureInputActionNames::Hook);
	if (!CastAction || !HookAction)
	{
		UE_LOG(LogLureFishing, Warning, TEXT("%s: the Cast/Hook input actions are missing (ULureInputSubsystem not running); fishing input is not bound."), *GetNameSafe(GetOwner()));
		return;
	}
	Input.BindAction(CastAction, ETriggerEvent::Started, this, &ULureFishingComponent::PressCast);
	Input.BindAction(CastAction, ETriggerEvent::Completed, this, &ULureFishingComponent::ReleaseCast);
	Input.BindAction(HookAction, ETriggerEvent::Started, this, &ULureFishingComponent::PressHook);
}

void ULureFishingComponent::PressCast()
{
	bCastButtonHeld = true; // held while a fish is on = reel (T-007)
	if (IsLineOut())
	{
		PressHook();
		return;
	}
	if (bCharging || bAwaitingCast)
	{
		return;
	}
	const ELureCastBlock Block = GetCastBlock();
	if (Block != ELureCastBlock::None)
	{
		LocalRefusal = Block;
		LocalRefusalTime = GetLocalTime();
		return;
	}
	bCharging = true;
	ChargeStartTime = GetLocalTime();
	Charge = 0.f;
}

void ULureFishingComponent::ReleaseCast()
{
	bCastButtonHeld = false;
	if (!bCharging)
	{
		return;
	}
	UpdateCharge();
	bCharging = false;

	float AimYaw = GetOwner() ? static_cast<float>(GetOwner()->GetActorRotation().Yaw) : 0.f;
	if (const APawn* Pawn = GetPawn())
	{
		AimYaw = static_cast<float>(Pawn->GetControlRotation().Yaw);
	}

	// Placeholder cast motion (or the cast montage) starts right away on the owner; the bobber follows the server's state.
	SwingFromPitch = GetProfile().CastSwingBackDeg * Charge;
	SwingStartTime = GetLocalTime();
	if (const ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(GetOwner()))
	{
		if (UAnimMontage* Montage = LureFishingPrivate::LoadIfExists(GetDefault<ULureFishingSettings>()->CastMontage))
		{
			if (UAnimInstance* Anim = Lure->GetFirstPersonArms() ? Lure->GetFirstPersonArms()->GetAnimInstance() : nullptr)
			{
				Anim->Montage_Play(Montage);
			}
		}
	}

	bAwaitingCast = true;
	AwaitedCastId = NetState.CastId;
	AwaitingSince = GetLocalTime();
	ServerCast(Charge, AimYaw);
	Charge = 0.f;
}

void ULureFishingComponent::PressHook()
{
	if (NetState.State == ELureFishingState::Waiting || NetState.State == ELureFishingState::Biting)
	{
		ServerHook();
	}
}

void ULureFishingComponent::RequestReelIn()
{
	if (IsLineOut())
	{
		ServerReelIn();
	}
}

void ULureFishingComponent::ServerCast_Implementation(float Charge01, float AimYawDegrees)
{
	AuthorityCast(Charge01, AimYawDegrees);
}

void ULureFishingComponent::ServerHook_Implementation()
{
	AuthorityHook();
}

void ULureFishingComponent::ServerReelIn_Implementation()
{
	AuthorityReelIn(ELureCastBlock::None);
}

void ULureFishingComponent::ServerSetReeling_Implementation(bool bReeling)
{
	AuthoritySetReeling(bReeling);
}

bool ULureFishingComponent::WantsToReel() const
{
	return (bCastButtonHeld || bReelButtonHeld) && NetState.State == ELureFishingState::Hooked;
}

void ULureFishingComponent::UpdateReelInput()
{
	const bool bWant = WantsToReel();
	if (bWant != bReelSent)
	{
		bReelSent = bWant;
		ServerSetReeling(bWant); // the only fight input a client sends; the server simulates and decides
	}
}

void ULureFishingComponent::AuthoritySetReeling(bool bReeling)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	bServerReeling = bReeling;
	if (FightNet.bActive)
	{
		FightNet.bReeling = bReeling;
	}
}

void ULureFishingComponent::UpdateCharge()
{
	if (bCharging)
	{
		Charge = FLureFishingRules::ChargeFromHoldTime(GetProfile(), static_cast<float>(GetLocalTime() - ChargeStartTime));
	}
}

// ---- Server ----

void ULureFishingComponent::SetFishTables(const FFishTables& InTables)
{
	Tables = InTables;
	bTablesSet = true;
	bTablesTried = true;
	TableRefs.Reset();
	for (const UDataTable* Table : { Tables.Species, Tables.Rarities, Tables.Modifiers, Tables.Stats })
	{
		if (Table)
		{
			TableRefs.Add(const_cast<UDataTable*>(Table));
		}
	}
}

void ULureFishingComponent::SetRandomSeed(int32 Seed)
{
	Rng.Initialize(Seed);
	bSeedSet = true;
}

bool ULureFishingComponent::EnsureFishTables()
{
	if (bTablesSet)
	{
		return true;
	}
	if (bTablesTried)
	{
		return false;
	}
	bTablesTried = true;
	FFishTables Loaded;
	FString Error;
	if (!GetDefault<UFishSettings>()->LoadTables(Loaded, Error))
	{
		UE_LOG(LogLureFishing, Warning, TEXT("Fishing: the fish tables are not available (%s); nothing will bite."), *Error);
		return false;
	}
	SetFishTables(Loaded);
	return true;
}

FLureFishingEnvironment ULureFishingComponent::MakeEnvironment() const
{
	const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
	FLureFishingEnvironment Environment;
	Environment.TimeOfDayHours = TimeOfDayOverride >= 0.f ? TimeOfDayOverride : ULureDayClockComponent::GetHourOr(this, Settings->DefaultTimeOfDayHours); // T-068a: the clock
	const FLureGearStats Gear = GetGearStats();
	Environment.BaitTag = BaitTag.IsValid() ? BaitTag
		: (Gear.BaitTag.IsValid() ? Gear.BaitTag : FGameplayTag::RequestGameplayTag(Settings->DefaultBait, /*ErrorIfNotFound*/ false));
	Environment.GearLuck = GearLuck + (FMath::IsFinite(Gear.Luck) ? FMath::Max(0.f, Gear.Luck) : 0.f);
	Environment.DefaultRegionTag = FGameplayTag::RequestGameplayTag(Settings->DefaultRegion, /*ErrorIfNotFound*/ false);
	return Environment;
}

void ULureFishingComponent::SetNetState(const FLureFishingNetState& NewState)
{
	const FLureFishingNetState Previous = NetState;
	NetState = NewState;
	HandleStateChanged(Previous); // the server's own view (listen server / standalone); clients get OnRep_NetState
}

void ULureFishingComponent::OnRep_NetState(const FLureFishingNetState& PreviousState)
{
	HandleStateChanged(PreviousState);
}

void ULureFishingComponent::BeginResult(FLureFishingNetState& State, ELureFishingResult Result, ELureCastBlock Reason, double Now) const
{
	State.LastResult = Result;
	State.ResultReason = Reason;
	State.ResultTime = Now;
	++State.ResultId;
}

void ULureFishingComponent::Refuse(ELureCastBlock Reason, double Now)
{
	FLureFishingNetState New = NetState;
	BeginResult(New, ELureFishingResult::Refused, Reason, Now);
	SetNetState(New);
}

bool ULureFishingComponent::AuthorityCast(float Charge01, float AimYawDegrees)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World || !Owner->HasAuthority())
	{
		return false;
	}
	const double Now = GetFishingTime();
	const ELureCastBlock Block = FLureFishingRules::GetCastBlock(GetConditions(), GetMovementRow());
	if (Block != ELureCastBlock::None)
	{
		Refuse(Block, Now);
		return false;
	}

	const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
	const FLureFishingRow& Row = GetProfile();
	const float CastCharge = FMath::IsFinite(Charge01) ? FMath::Clamp(Charge01, 0.f, 1.f) : 0.f;
	// A client sends any float: non-finite falls back to the actor's facing; huge finite values are wrapped to (-180, 180].
	const float Yaw = FRotator::NormalizeAxis(FMath::IsFinite(AimYawDegrees) ? AimYawDegrees : static_cast<float>(Owner->GetActorRotation().Yaw));
	const FRotator Aim(0.f, Yaw, 0.f);

	// The line leaves about at the rod tip (kept out of walls right in front of the player).
	const FVector Eye = GetEyeLocation();
	FVector Origin = Eye + Aim.RotateVector(Settings->RodTipOffsetFromEye);
	{
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(LureCastOrigin), false, Owner);
		FHitResult Hit;
		if (FLureFishingSpots::TraceCast(World, Hit, Eye, Origin, Params))
		{
			Origin = Hit.Location - (Origin - Eye).GetSafeNormal() * 5.f;
		}
	}

	const float Distance = FLureFishingRules::CastDistance(Row, CastCharge) * FMath::Max(0.f, GetGearStats().CastDistanceMultiplier);
	const FLureCastLanding Landing = FLureFishingSpots::ResolveLanding(World, Owner, Origin, FVector2D(Eye.X, Eye.Y),
		FVector2D(Aim.Vector().X, Aim.Vector().Y), Distance, *Settings, &Row);

	// T-027: every body of water can be fished; the water area under the bobber decides its habitat (fishing-water-rules.md).
	WaterContext = FLureWaterQuery::DescribeWater(World, Landing.Rest, Landing.bOnWater, Landing.WaterZ);
	HotSpotBonus = FLureHotSpotBonus(); // a hot spot counts where the bobber lands (LandBobber)
	PendingFish = FFishInstance();
	NextBiteTime = -1.0;
	NibbleSchedule.Reset();
	NextNibbleIndex = 0;

	FLureFishingNetState New = NetState;
	New.State = ELureFishingState::Casting;
	++New.CastId;
	New.CastOrigin = Origin;
	New.BobberRest = Landing.Rest;
	New.FlightTime = FLureFishingRules::CastFlightTime(Row, static_cast<float>(FVector::Dist(Origin, Landing.Rest)));
	New.StateStartTime = Now;
	New.bOnWater = Landing.bOnWater;
	New.bNoFishHere = false;
	New.SpotId = WaterContext.AreaId;
	New.Water = FLureBobberWater();
	SetNetState(New);

	UE_LOG(LogLureFishing, Verbose, TEXT("%s casts %.0f cm (charge %.2f): %s."), *Owner->GetName(), Distance, CastCharge,
		Landing.bOnWater ? *FString::Printf(TEXT("water %s (%s, %.0f cm deep)"), WaterContext.AreaId.IsNone() ? TEXT("default") : *WaterContext.AreaId.ToString(),
			*WaterContext.HabitatTag.ToString(), WaterContext.DepthCm) : TEXT("land"));
	return true;
}

void ULureFishingComponent::AuthorityHook()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	const double Now = GetFishingTime();
	const FLureFishingRow& Row = GetProfile();
	switch (NetState.State)
	{
	case ELureFishingState::Biting:
		if (FLureFishingRules::IsInHookWindow(Row, NetState.StateStartTime, Now, GetHookGrace()))
		{
			Hook(Now);
		}
		else
		{
			Miss(Now);
		}
		break;

	case ELureFishingState::Waiting:
		// Nothing can bite (on land, no spot, nothing fits): a press always reels in.
		if (!NetState.bOnWater || NextBiteTime < 0.0 || NetState.bNoFishHere)
		{
			AuthorityReelIn(ELureCastBlock::None);
			break;
		}
		switch (Row.EarlyHook)
		{
		case ELureEarlyHookRule::ReelIn:
			AuthorityReelIn(ELureCastBlock::None);
			break;
		case ELureEarlyHookRule::Spook:
		{
			const double Delay = FMath::Max(0.f, Row.SpookDelay);
			// The scheduled bite moves back by SpookDelay from when it was due; nibbles still to come move with it.
			NextBiteTime += Delay;
			for (int32 Index = NextNibbleIndex; Index < NibbleSchedule.Num(); ++Index)
			{
				NibbleSchedule[Index] += Delay;
			}
			FLureFishingNetState New = NetState;
			BeginResult(New, ELureFishingResult::Spooked, ELureCastBlock::None, Now);
			SetNetState(New);
			OnFishingEvent.Broadcast(ELureFishingResult::Spooked, FFishInstance());
			break;
		}
		case ELureEarlyHookRule::Ignore:
		default:
			break;
		}
		break;

	default:
		break; // casting: too early to do anything; hooked: the reel fight (T-007) takes over
	}
}

void ULureFishingComponent::AuthorityReelIn(ELureCastBlock Reason)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || NetState.State == ELureFishingState::Idle)
	{
		return;
	}
	const double Now = GetFishingTime();
	const bool bHadFish = NetState.State == ELureFishingState::Hooked && HookedFish.IsValid();
	const FFishInstance Lost = HookedFish;
	if (bHadFish)
	{
		// T-032c: every fight end logs its reason (EndFight logs snaps, spools and thrown hooks; this is the line rules' path).
		const TCHAR* Why = LureFishingPrivate::ReasonText(Reason);
		UE_LOG(LogLureFishing, Log, TEXT("%s: the line came in (%s) after %.1f s (%.0f cm out) - %s lost."), *GetNameSafe(GetOwner()),
			*Why ? Why : TEXT("reeled in"), Fight.Elapsed, Fight.LineOut, *Lost.SpeciesId.ToString());
	}
	PendingFish = FFishInstance();
	HookedFish = FFishInstance();
	NextBiteTime = -1.0;
	NibbleSchedule.Reset();
	NextNibbleIndex = 0;
	bServerReeling = false;
	FightNet.bActive = false;
	FightNet.bReeling = false;

	FLureFishingNetState New = NetState;
	New.State = ELureFishingState::Idle;
	New.StateStartTime = Now;
	BeginResult(New, bHadFish ? ELureFishingResult::Lost : ELureFishingResult::ReeledIn, Reason, Now);
	SetNetState(New);
	OnFishingEvent.Broadcast(New.LastResult, Lost);
}

void ULureFishingComponent::AuthorityOwnerTeleported()
{
	// T-028b (O4): a teleport ends a fight in progress (the line can't follow); the fish is lost. A line without a fish is left to
	// the normal rules (the bobber distance rule brings it in if the teleport went far).
	if (GetOwner() && GetOwner()->HasAuthority() && NetState.State == ELureFishingState::Hooked)
	{
		AuthorityReelIn(ELureCastBlock::Teleported);
	}
}

void ULureFishingComponent::AuthorityOwnerUnpossessed()
{
	// T-028b (O5): nobody holds this rod any more, so its fight ends (a fight nobody steers must not run on).
	if (GetOwner() && GetOwner()->HasAuthority() && NetState.State == ELureFishingState::Hooked)
	{
		AuthorityReelIn(ELureCastBlock::Unpossessed);
	}
}

void ULureFishingComponent::ServerTick(double Now)
{
	if (NetState.State == ELureFishingState::Idle)
	{
		return;
	}

	// Rules that bring the line in: sprinting, swimming (the climb out included), climbing, crawling with a tucked rod,
	// too far from the bobber. They also end a fight in progress (the fish is lost; reel-fight-rules.md). While a fish is
	// on, the fight has its own line rules (spool length), so only the bobber distance rule does not apply.
	const bool bFighting = NetState.State == ELureFishingState::Hooked && FightNet.bActive;
	const float BobberDistance = (GetOwner() && !bFighting) ? static_cast<float>(FVector::Dist2D(GetOwner()->GetActorLocation(), NetState.BobberRest)) : 0.f;
	const ELureCastBlock Cancel = FLureFishingRules::GetLineCancel(GetConditions(), GetMovementRow(), GetProfile(), BobberDistance);
	if (Cancel != ELureCastBlock::None)
	{
		AuthorityReelIn(Cancel);
		return;
	}

	const FLureFishingRow& Row = GetProfile();
	switch (NetState.State)
	{
	case ELureFishingState::Casting:
		if (Now >= NetState.StateStartTime + NetState.FlightTime)
		{
			LandBobber(Now);
		}
		break;

	case ELureFishingState::Waiting:
		UpdateNibbles(Now);
		if (NextBiteTime >= 0.0 && Now >= NextBiteTime)
		{
			TryBite(Now);
		}
		break;

	case ELureFishingState::Biting:
		if (Now > FLureFishingRules::HookDeadline(Row, NetState.StateStartTime, GetHookGrace()))
		{
			Miss(Now);
		}
		break;

	case ELureFishingState::Hooked:
		if (Row.AutoLandDelay > 0.f)
		{
			if (Now >= NetState.StateStartTime + Row.AutoLandDelay)
			{
				LandFish(Now); // debug/tests placeholder: no fight
			}
		}
		else if (FightNet.bActive)
		{
			UpdateFight(Now);
		}
		break;

	default:
		break;
	}
}

void ULureFishingComponent::LandBobber(double Now)
{
	FLureFishingNetState New = NetState;
	New.State = ELureFishingState::Waiting;
	New.StateStartTime = Now;
	// T-027: a cast that lands in a hot spot keeps its bonus for the whole cast (fishing-water-rules.md, "Hot spots").
	HotSpotBonus = New.bOnWater ? FLureWaterQuery::FindHotSpotBonusAt(GetWorld(), FVector2D(New.BobberRest.X, New.BobberRest.Y), Now) : FLureHotSpotBonus();
	New.Water.HotSpotType = HotSpotBonus.TypeId;
	// Can anything bite here now (depth, this water's species at this time, the bait)? If not: no nibbles (they are the tells of
	// a coming bite) and the reason is set now for the HUD. The check is seed-independent.
	FLureBiteDecision Check;
	Check.Reason = New.bOnWater ? ELureNoBiteReason::NoSpecies : ELureNoBiteReason::NotWater;
	if (New.bOnWater && EnsureFishTables())
	{
		Check = FLureWaterRules::DecideBite(Tables, WaterContext, HotSpotBonus, MakeEnvironment(), FLureBiteRules::FromSettings(), 0);
	}
	if (!Check.CanBite())
	{
		LastRollContext = Check.Context; // what was checked, for debugging and tests (water that fits records the real roll at the bite)
	}
	New.bNoFishHere = New.bOnWater && !Check.CanBite();
	New.Water.NoBiteReason = New.bOnWater ? Check.Reason : ELureNoBiteReason::None;
	SetNetState(New);
	if (Check.CanBite())
	{
		ScheduleBite(Now, /*bAfterMiss*/ false);
	}
	else if (Check.Reason == ELureNoBiteReason::NoSpecies || Check.Reason == ELureNoBiteReason::WrongBait)
	{
		// Look again every BiteWaitMax seconds (the time of day moves on); TryBite bites then if something fits.
		NextBiteTime = Now + FMath::Max(1.f, GetProfile().BiteWaitMax);
	}
}

void ULureFishingComponent::ScheduleBite(double Now, bool bAfterMiss)
{
	const FLureFishingRow& Row = GetProfile();
	// A hot spot's fish bite sooner (T-027 BiteWaitScale; 1 without a hot spot, so the draws and waits are unchanged).
	const float Wait = FLureFishingRules::RandomBiteWait(Row, Rng, bAfterMiss) * (HotSpotBonus.IsActive() ? HotSpotBonus.BiteWaitScale : 1.f);
	NextBiteTime = Now + Wait;
	NibbleSchedule.Reset();
	for (const float Time : FLureFishingRules::NibbleTimes(Row, Rng, Wait))
	{
		NibbleSchedule.Add(Now + Time);
	}
	NextNibbleIndex = 0;
}

void ULureFishingComponent::UpdateNibbles(double Now)
{
	bool bChanged = false;
	FLureFishingNetState New = NetState;
	while (NextNibbleIndex < NibbleSchedule.Num() && Now >= NibbleSchedule[NextNibbleIndex])
	{
		++New.NibbleId;
		New.LastNibbleTime = NibbleSchedule[NextNibbleIndex];
		++NextNibbleIndex;
		bChanged = true;
	}
	if (bChanged)
	{
		SetNetState(New);
	}
}

void ULureFishingComponent::TryBite(double Now)
{
	NextBiteTime = -1.0;
	FLureFishingNetState New = NetState;
	if (!EnsureFishTables())
	{
		New.bNoFishHere = true;
		New.Water.NoBiteReason = ELureNoBiteReason::NoSpecies;
		SetNetState(New);
		return;
	}

	const int32 Seed = static_cast<int32>(Rng.GetUnsignedInt());
	// T-027: the water (area habitat, region, luck; a gap fallback habitat if the data has none at this hour) + the hot spot.
	const FLureBiteDecision Decision = FLureWaterRules::DecideBite(Tables, WaterContext, HotSpotBonus, MakeEnvironment(), FLureBiteRules::FromSettings(), Seed);
	LastRollContext = Decision.Context;
	FFishInstance Fish;
	if (Decision.CanBite() && FLureFishingRules::DecideBite(Tables, LastRollContext, Fish))
	{
		PendingFish = Fish;
		New.State = ELureFishingState::Biting;
		New.StateStartTime = Now;
		New.bNoFishHere = false;
		New.Water.NoBiteReason = ELureNoBiteReason::None;
		SetNetState(New);
		UE_LOG(LogLureFishing, Verbose, TEXT("%s: bite (%s)."), *GetNameSafe(GetOwner()), *Fish.ToString());
		return;
	}

	// Nothing fits this water, time or bait right now: say why, and look again later (the time of day moves on).
	New.bNoFishHere = true;
	New.Water.NoBiteReason = Decision.CanBite() ? ELureNoBiteReason::NoSpecies : Decision.Reason;
	SetNetState(New);
	if (Decision.Reason != ELureNoBiteReason::TooShallow && Decision.Reason != ELureNoBiteReason::NotWater)
	{
		NextBiteTime = Now + FMath::Max(1.f, GetProfile().BiteWaitMax);
	}
}

void ULureFishingComponent::Hook(double Now)
{
	HookedFish = PendingFish;
	PendingFish = FFishInstance();
	NextBiteTime = -1.0;
	FLureFishingNetState New = NetState;
	New.State = ELureFishingState::Hooked;
	New.StateStartTime = Now;
	BeginResult(New, ELureFishingResult::Hooked, ELureCastBlock::None, Now);
	SetNetState(New);
	if (GetProfile().AutoLandDelay <= 0.f)
	{
		BeginFight(Now);
	}
	OnFishingEvent.Broadcast(ELureFishingResult::Hooked, HookedFish);
}

bool ULureFishingComponent::AuthorityHookFish(const FFishInstance& Fish)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Fish.IsValid()
		|| (NetState.State != ELureFishingState::Waiting && NetState.State != ELureFishingState::Biting))
	{
		return false;
	}
	PendingFish = Fish;
	Hook(GetFishingTime());
	return true;
}

// ---- Reel fight (T-007) ----

void ULureFishingComponent::BeginFight(double Now)
{
	ResolveFightTables();
	FName SpeciesPattern = NAME_None;
	if (EnsureFishTables())
	{
		if (const FFishSpeciesRow* Species = Tables.FindSpecies(HookedFish.SpeciesId))
		{
			SpeciesPattern = Species->FightPatternId;
		}
	}
	FName PatternId = NAME_None;
	const FLureFightPatternRow Pattern = FindPattern(SpeciesPattern, PatternId);
	const FLureFightFish FishStats = FLureFight::MakeFish(HookedFish, FightTuning, PlayerLevel, GetDefault<UFishSettings>()->LevelScaling);
	const FLureGearStats Gear = GetGearStats();
	const float Start = GetOwner() ? static_cast<float>(FVector::Dist2D(GetOwner()->GetActorLocation(), NetState.BobberRest)) : 0.f;
	FLureFight::Begin(Fight, FishStats, Pattern, PatternId, Gear, FightTuning, FLureFight::FightSeed(HookedFish.Seed), Start);
	FightLastTime = Now;
	// T-028: a new fight starts with the rod level and centered (the owner resets its aim too); the reel step carries over.
	ServerRodPitch = 0.f;
	ServerRodYaw = 0.f;
	if (ServerPendingReelStep != INDEX_NONE)
	{
		ServerReelStep = ServerPendingReelStep; // the owner's last wheel pick, held back by the step rate limit, is the new fight's step
		ServerPendingReelStep = INDEX_NONE;
	}
	Fight.ReelStep = FLureFight::ClampReelStep(ServerReelStep, FightTuning);

	const uint8 NextId = static_cast<uint8>(FightNet.FightId + 1);
	FightNet = FLureFightNetState();
	FightNet.bActive = true;
	FightNet.FightId = NextId;
	PublishFight();
	UE_LOG(LogLureFishing, Log, TEXT("%s: fight with %s (pattern %s): pull %.1f, speed %.0f cm/s, stamina %.0f, level x%.2f; gear %s/%s/%s (power %.1f, drag %.1f, line %.1f), %.0f cm out."),
		*GetNameSafe(GetOwner()), *HookedFish.SpeciesId.ToString(), PatternId.IsNone() ? TEXT("built-in") : *PatternId.ToString(), FishStats.BasePull,
		FishStats.BaseSpeed, FishStats.StaminaPool, FishStats.LevelMultiplier, *Gear.RodId.ToString(), *Gear.LineId.ToString(), *Gear.HookId.ToString(),
		Gear.RodPower, Gear.Drag, Gear.LineStrength, Start);
}

void ULureFishingComponent::UpdateFight(double Now)
{
	const float Delta = static_cast<float>(FMath::Clamp(Now - FightLastTime, 0.0, 0.5));
	FightLastTime = Now;
	ApplyPendingReelStep(Now); // T-028b: a reel-step change held back by the server's rate limit
	const FLureFightInput Input = GetServerFightInput(); // the reel button + the rod aim and reel step (T-028)
	const ELureFightOutcome Outcome = FLureFight::Advance(Fight, Input, Delta);
	PublishFight();
	if (Outcome != ELureFightOutcome::None)
	{
		EndFight(Outcome, Now);
	}
}

void ULureFishingComponent::PublishFight()
{
	const FLureFishFightRow& Tuning = Fight.Tuning;
	FightNet.bReeling = bServerReeling;
	FightNet.bExhausted = Fight.bExhausted;
	FightNet.Outcome = Fight.Outcome;
	FightNet.PatternId = Fight.PatternId;
	FightNet.MoveId = Fight.GetMoveId();
	FightNet.Tension = Fight.Tension;
	FightNet.LineStrength = Fight.Gear.LineStrength;
	FightNet.SlackTension = FLureFight::SlackTension(Fight.Fish, Tuning);
	FightNet.Stamina = Fight.Stamina;
	FightNet.LineOut = Fight.LineOut;
	FightNet.SpoolLength = Fight.Gear.SpoolLength;
	FightNet.Depth = Fight.Depth;
	FightNet.SideDeg = Fight.SideDeg;
	FightNet.SnapProgress = FMath::Clamp(Fight.OverTime / FMath::Max(1.0e-3f, Tuning.SnapGraceTime), 0.f, 1.f);
	FightNet.SlackProgress = FMath::Clamp(Fight.SlackTime / FMath::Max(1.0e-3f, FLureFight::SlackGrace(Fight.Gear, Tuning)), 0.f, 1.f);
	// T-028: the rod input the fight ran with (other players draw this rod and line) and the fish's sideways run (HUD hint).
	FightNet.RodPitch = Fight.RodPitch;
	FightNet.RodYaw = Fight.RodYaw;
	FightNet.ReelStep = static_cast<uint8>(FMath::Clamp(Fight.ReelStep, 0, 255));
	FightNet.RunSide = FLureRodControl::RunSideFromDirection(Fight.RunDir);
}

void ULureFishingComponent::EndFight(ELureFightOutcome Outcome, double Now)
{
	FightNet.bActive = false;
	FightNet.Outcome = Outcome;
	FightNet.bReeling = false;
	bServerReeling = false;
	if (Outcome == ELureFightOutcome::Landed)
	{
		LandFish(Now);
		return;
	}

	UE_LOG(LogLureFishing, Log, TEXT("%s: %s after %.1f s (%s; tension %.1f / line %.1f, %.0f cm out) - %s lost."), *GetNameSafe(GetOwner()),
		LureFishingPrivate::OutcomeText(Outcome), Fight.Elapsed, Fight.bReeling ? TEXT("reeling") : TEXT("not reeling"), Fight.Tension,
		Fight.Gear.LineStrength, Fight.LineOut, *HookedFish.SpeciesId.ToString());
	const FFishInstance Lost = HookedFish;
	const ELureFishingResult Result = Outcome == ELureFightOutcome::ThrewHook ? ELureFishingResult::ThrewHook : ELureFishingResult::Snapped;
	HookedFish = FFishInstance();
	PendingFish = FFishInstance();
	NextBiteTime = -1.0;
	NibbleSchedule.Reset();
	NextNibbleIndex = 0;
	FLureFishingNetState New = NetState;
	New.State = ELureFishingState::Idle;
	New.StateStartTime = Now;
	BeginResult(New, Result, ELureCastBlock::None, Now);
	SetNetState(New);
	OnFishingEvent.Broadcast(Result, Lost);
}

void ULureFishingComponent::Miss(double Now)
{
	PendingFish = FFishInstance(); // the bite and its fish are gone for good
	FLureFishingNetState New = NetState;
	BeginResult(New, ELureFishingResult::Missed, ELureCastBlock::None, Now);
	if (GetProfile().MissEndsCast)
	{
		New.State = ELureFishingState::Idle;
		New.StateStartTime = Now;
		NextBiteTime = -1.0;
		NibbleSchedule.Reset();
		SetNetState(New);
	}
	else
	{
		New.State = ELureFishingState::Waiting;
		New.StateStartTime = Now;
		SetNetState(New);
		ScheduleBite(Now, /*bAfterMiss*/ true);
	}
	OnFishingEvent.Broadcast(ELureFishingResult::Missed, FFishInstance());
}

void ULureFishingComponent::LandFish(double Now)
{
	LastLandedFish = HookedFish;
	HookedFish = FFishInstance();
	FLureFishingNetState New = NetState;
	New.State = ELureFishingState::Idle;
	New.StateStartTime = Now;
	BeginResult(New, ELureFishingResult::Landed, ELureCastBlock::None, Now);
	SetNetState(New);
	FightNet.bActive = false;
	bServerReeling = false;

	// The catch log (species, rarity, weight, value), then the hand-off to the cooler (T-010) and anyone else listening.
	TArray<FString> Mods;
	for (const FName& Mod : LastLandedFish.ModifierIds)
	{
		Mods.Add(Mod.ToString());
	}
	UE_LOG(LogLureFish, Log, TEXT("Catch: %s landed %s (%s), %.2f kg, %d coins, level %d%s%s."), *GetNameSafe(GetOwner()),
		*LastLandedFish.SpeciesId.ToString(), *LastLandedFish.RarityId.ToString(), LastLandedFish.WeightKg, LastLandedFish.Value, LastLandedFish.Level,
		Mods.Num() > 0 ? *FString::Printf(TEXT(", [%s]"), *FString::Join(Mods, TEXT(", "))) : TEXT(""),
		Fight.Elapsed > 0.f && Fight.Outcome == ELureFightOutcome::Landed ? *FString::Printf(TEXT(", fight %.1f s"), Fight.Elapsed) : TEXT(""));
	// T-030 hand-off (catch-handling-rules.md), exactly once per landed fish (LandFish only runs on the server): the XP now
	// and the fish on the hook. A pawn without progression or hands (tests, a bare character) just logs what it skips.
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ULureCatchLibrary::HandleFishLanded(GetOwner(), LastLandedFish);
	}
	OnFishingEvent.Broadcast(ELureFishingResult::Landed, LastLandedFish);
	OnFishLanded.Broadcast(this, LastLandedFish);
	OnFishLandedNative.Broadcast(this, LastLandedFish);
}

// ---- Tick ----

void ULureFishingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ServerTick(GetFishingTime());
	}

	if (IsOwnerLocallyControlled())
	{
		if (bCharging)
		{
			UpdateCharge();
			const ELureCastBlock Block = GetCastBlock();
			if (Block != ELureCastBlock::None)
			{
				bCharging = false; // started sprinting, fell, began to crawl...
				Charge = 0.f;
				LocalRefusal = Block;
				LocalRefusalTime = GetLocalTime();
			}
		}
		if (bAwaitingCast && GetLocalTime() - AwaitingSince > 2.0)
		{
			bAwaitingCast = false; // the server never answered (lost connection); let the player try again
		}
		UpdateReelInput();
		UpdateRodSteering(DeltaTime); // T-028: new-fight reset, the rod input to the server, the camera following the fish
	}

	if (ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(GetOwner()))
	{
		Lure->SetHoldingRod(IsRodInHand());
	}

	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateRodAimVisual(DeltaTime); // T-028: the eased rod aim (arms aim offset, other players' rod and line)
		UpdateVisuals(DeltaTime);
	}
}

// ---- Visuals and feedback ----

void ULureFishingComponent::HandleStateChanged(const FLureFishingNetState& Previous)
{
	if (bAwaitingCast && (NetState.CastId != AwaitedCastId || (NetState.ResultId != Previous.ResultId && NetState.LastResult == ELureFishingResult::Refused)))
	{
		bAwaitingCast = false;
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
	const FLureFishingRow& Row = GetProfile();

	if (NetState.State == ELureFishingState::Casting && NetState.CastId != Previous.CastId)
	{
		PlayFeedbackSound(Settings->CastSound, NetState.CastOrigin);
	}
	if (NetState.NibbleId != Previous.NibbleId)
	{
		PlayFeedbackSound(Settings->NibbleSound, NetState.BobberRest);
	}
	const bool bNewBite = NetState.State == ELureFishingState::Biting
		&& (Previous.State != ELureFishingState::Biting || Previous.StateStartTime != NetState.StateStartTime);
	if (bNewBite)
	{
		PlayFeedbackSound(Settings->BiteSound, NetState.BobberRest);
		if (IsOwnerLocallyControlled() && Row.BiteRumbleIntensity > 0.f && Row.BiteRumbleDuration > 0.f)
		{
			if (APlayerController* Controller = Cast<APlayerController>(GetPawn() ? GetPawn()->GetController() : nullptr))
			{
				Controller->PlayDynamicForceFeedback(Row.BiteRumbleIntensity, Row.BiteRumbleDuration, true, true, true, true);
			}
		}
	}
	if (NetState.ResultId != Previous.ResultId && NetState.LastResult == ELureFishingResult::Hooked)
	{
		PlayFeedbackSound(Settings->HookSound, NetState.BobberRest);
		if (const ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(GetOwner()); Lure && IsOwnerLocallyControlled())
		{
			if (UAnimMontage* Montage = LureFishingPrivate::LoadIfExists(Settings->HookMontage))
			{
				if (UAnimInstance* Anim = Lure->GetFirstPersonArms() ? Lure->GetFirstPersonArms()->GetAnimInstance() : nullptr)
				{
					Anim->Montage_Play(Montage);
				}
			}
		}
		PlayArmsMontage(Settings->FightMontage); // T-007 slot (none by default)
	}
	if (Previous.State == ELureFishingState::Hooked && NetState.State != ELureFishingState::Hooked)
	{
		PlayArmsMontage(Settings->FightMontage, /*bStop*/ true);
	}
	if (NetState.ResultId != Previous.ResultId && NetState.LastResult == ELureFishingResult::Landed)
	{
		PlayArmsMontage(Settings->LandMontage);
	}
	if (NetState.ResultId != Previous.ResultId && NetState.LastResult == ELureFishingResult::Snapped && Line)
	{
		Line->Snap(); // T-032: the broken line whips back toward the rod, then is gone
	}
}

void ULureFishingComponent::PlayArmsMontage(const TSoftObjectPtr<UAnimMontage>& Montage, bool bStop) const
{
	const ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(GetOwner());
	if (!Lure || !IsOwnerLocallyControlled() || Montage.IsNull())
	{
		return;
	}
	UAnimMontage* Loaded = LureFishingPrivate::LoadIfExists(Montage);
	UAnimInstance* Anim = (Loaded && Lure->GetFirstPersonArms()) ? Lure->GetFirstPersonArms()->GetAnimInstance() : nullptr;
	if (!Anim)
	{
		return;
	}
	if (bStop)
	{
		if (Anim->Montage_IsPlaying(Loaded))
		{
			Anim->Montage_Stop(0.2f, Loaded);
		}
	}
	else if (!Anim->Montage_IsPlaying(Loaded))
	{
		Anim->Montage_Play(Loaded);
	}
}

void ULureFishingComponent::UpdateFightMontages()
{
	// Reel montage (T-007 slot): looped while the player holds reel during a fight.
	const bool bReel = FightNet.bActive && NetState.State == ELureFishingState::Hooked && (IsOwnerLocallyControlled() ? WantsToReel() : FightNet.bReeling);
	if (bReel != bReelMontagePlaying)
	{
		bReelMontagePlaying = bReel;
		PlayArmsMontage(GetDefault<ULureFishingSettings>()->ReelMontage, /*bStop*/ !bReel);
	}
}

void ULureFishingComponent::PlayFeedbackSound(const TSoftObjectPtr<USoundBase>& Sound, const FVector& Location) const
{
	if (USoundBase* Loaded = LureFishingPrivate::LoadIfExists(Sound))
	{
		UGameplayStatics::PlaySoundAtLocation(this, Loaded, Location);
	}
}

void ULureFishingComponent::UpdateVisuals(float DeltaTime)
{
	EnsureRod();
	UpdateRod(DeltaTime);
	UpdateFightMontages();

	if (IsLineOut())
	{
		EnsureBobberAndLine();
	}

	FVector BobberLocation;
	FRotator BobberRotation;
	ComputeBobberPose(GetFishingTime(), BobberLocation, BobberRotation);

	const FLureFishingRow* Row = IsLineOut() ? &GetProfile() : nullptr;
	if (BobberMesh)
	{
		if (Row)
		{
			BobberMesh->SetWorldLocationAndRotation(BobberLocation, BobberRotation);
			BobberMesh->SetWorldScale3D(FVector(Row->BobberScale));
			BobberMesh->SetVisibility(true);
		}
		else
		{
			BobberMesh->SetVisibility(false);
		}
	}

	if (Line)
	{
		if (Row)
		{
			// T-032: the line simulates itself (docs/specs/fishing-line.md); it only needs its ends, tension, water and viewer.
			const FName Socket = GetDefault<ULureFishingSettings>()->BobberLineSocket;
			const FVector End = (BobberMesh && BobberMesh->DoesSocketExist(Socket)) ? BobberMesh->GetSocketLocation(Socket) : BobberLocation + FVector::UpVector * BobberTop;
			FVector ViewLocation;
			float Fov = 90.f;
			GetViewer(ViewLocation, Fov);
			Line->SetViewer(ViewLocation, Fov);
			Line->SetWidthRule(Row->LinePixelWidth, GetDefault<ULureFishingSettings>()->LineReferenceScreenWidth, Row->LineMinWidth);
			// In a fight the line is straight from DT_FishFight TautTension of the line's strength (T-032b; FLureFight::LineTension).
			Line->SetTension(FLureFishingLineRules::StateTension(NetState.State, FightNet.bActive,
				FLureFight::LineTension(FightNet.GetTension01(), GetFightTuning()), Line->GetTuning()));
			if (NetState.bOnWater)
			{
				Line->SetWaterSurfaceZ(static_cast<float>(NetState.BobberRest.Z)); // the bobber rests on the surface
			}
			else
			{
				Line->ClearWaterSurfaceZ();
			}
			Line->SetEndpoints(GetLineStart(), End);
		}
		else
		{
			Line->Hide();
			if (Line->GetEndActor())
			{
				// T-034: a fish hangs on the line (T-032), so it is still drawn: keep its viewer current. Without this the widths
				// were sized for where the eye was when the line last was out (a GiveFish far from the last cast: centimetres thick).
				FVector ViewLocation;
				float Fov = 90.f;
				GetViewer(ViewLocation, Fov);
				Line->SetViewer(ViewLocation, Fov);
				const FLureFishingRow& HangRow = GetProfile();
				Line->SetWidthRule(HangRow.LinePixelWidth, GetDefault<ULureFishingSettings>()->LineReferenceScreenWidth, HangRow.LineMinWidth);
			}
		}
	}
}

void ULureFishingComponent::EnsureRod()
{
	if (RodMesh || bRodTried || !IsOwnerLocallyControlled())
	{
		return;
	}
	const ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(GetOwner());
	USkeletalMeshComponent* Arms = Lure ? Lure->GetFirstPersonArms() : nullptr;
	if (!Arms || !Arms->GetSkeletalMeshAsset())
	{
		return; // try again once the arms are loaded
	}
	bRodTried = true;

	const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
	UStaticMesh* Mesh = LureFishingPrivate::LoadIfExists(Settings->RodMesh);
	if (!Mesh)
	{
		UE_LOG(LogLureFishing, Log, TEXT("Rod mesh '%s' is not imported yet; no rod shown."), *Settings->RodMesh.ToString());
		return;
	}
	if (Arms->GetBoneIndex(Settings->RodAttachBone) == INDEX_NONE)
	{
		UE_LOG(LogLureFishing, Warning, TEXT("The arms mesh has no bone '%s'; the rod is not attached."), *Settings->RodAttachBone.ToString());
		return;
	}

	RodMesh = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("FishingRod"), RF_Transient);
	RodMesh->SetStaticMesh(Mesh);
	RodMesh->SetOnlyOwnerSee(true);
	RodMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	RodMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	RodMesh->SetCanEverAffectNavigation(false);
	RodMesh->SetCastShadow(false);
	// World scale kept: the rod stays 1:1 whatever scale the arms' bones carry (the current import has a 100x root bone).
	RodMesh->SetUsingAbsoluteScale(true);
	RodMesh->SetRelativeScale3D(FVector::OneVector);
	RodMesh->RegisterComponent();
	RodMesh->AttachToComponent(Arms, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Settings->RodAttachBone);
	RodMesh->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
}

void ULureFishingComponent::UpdateRod(float DeltaTime)
{
	if (!RodMesh)
	{
		return;
	}
	const bool bInHand = IsRodInHand();
	if (RodMesh->IsVisible() != bInHand)
	{
		RodMesh->SetVisibility(bInHand);
	}

	// Placeholder cast motion: tip back with the charge, whip forward on release, settle. A cast montage replaces it.
	const FLureFishingRow& Row = GetProfile();
	float Pitch = 0.f;
	if (GetDefault<ULureFishingSettings>()->CastMontage.IsNull())
	{
		if (bCharging)
		{
			Pitch = Row.CastSwingBackDeg * Charge;
		}
		else if (SwingStartTime >= 0.0)
		{
			const float T = static_cast<float>((GetLocalTime() - SwingStartTime) / FMath::Max(0.01f, Row.CastSwingTime));
			if (T >= 1.f)
			{
				SwingStartTime = -1.0;
			}
			else if (T < 0.35f)
			{
				Pitch = FMath::Lerp(SwingFromPitch, -Row.CastSwingForwardDeg, FMath::InterpEaseOut(0.f, 1.f, T / 0.35f, 2.f));
			}
			else
			{
				Pitch = FMath::Lerp(-Row.CastSwingForwardDeg, 0.f, FMath::InterpEaseInOut(0.f, 1.f, (T - 0.35f) / 0.65f, 2.f));
			}
		}
	}
	// Placeholder rod bend (T-007): the tip dips toward the fish with the tension and shakes over the line's strength. When the
	// fight ends the rod straightens over TensionFallTime, not in one frame (T-032b: that jerk flung the landed fish over the tip).
	if (NetState.State == ELureFishingState::Hooked && FightNet.bActive)
	{
		RodBendTension01 = FMath::IsFinite(FightNet.GetTension01()) ? FMath::Max(0.f, FightNet.GetTension01()) : 0.f;
	}
	else
	{
		RodBendTension01 = FLureFight::EaseTension(FMath::Min(RodBendTension01, 1.f), 0.f, DeltaTime, GetFightTuning());
		RodBendTension01 = RodBendTension01 < 1.0e-3f ? 0.f : RodBendTension01;
	}
	if (RodBendTension01 > 0.f)
	{
		Pitch += FLureFight::RodPitch(RodBendTension01, static_cast<float>(GetLocalTime()), GetFightTuning());
	}
	// T-028: the rod follows the player's aim (placeholder turn, until the arms play the rod-aim aim offset and carry it).
	const FRotator Aim = ArmsPlayRodAim() ? FRotator::ZeroRotator : FLureRodControl::RodLook(RodAimVisual.Y, RodAimVisual.X, GetFightTuning());
	RodMesh->SetRelativeRotation(FRotator(Pitch + Aim.Pitch, Aim.Yaw, 0.f));
}

void ULureFishingComponent::EnsureBobberAndLine()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
	const FLureFishingRow& Row = GetProfile();

	if (!BobberMesh && !bBobberTried)
	{
		bBobberTried = true;
		UStaticMesh* Mesh = LureFishingPrivate::LoadIfExists(Settings->BobberMesh);
		if (!Mesh)
		{
			UE_LOG(LogLureFishing, Log, TEXT("Bobber mesh '%s' is not imported yet; no bobber shown."), *Settings->BobberMesh.ToString());
		}
		else
		{
			BobberMesh = NewObject<UStaticMeshComponent>(Owner, TEXT("FishingBobber"), RF_Transient);
			BobberMesh->SetStaticMesh(Mesh);
			BobberMesh->SetUsingAbsoluteLocation(true);
			BobberMesh->SetUsingAbsoluteRotation(true);
			BobberMesh->SetUsingAbsoluteScale(true);
			BobberMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			BobberMesh->SetCanEverAffectNavigation(false);
			BobberMesh->SetCastShadow(false);
			BobberMesh->SetVisibility(false);
			BobberMesh->SetupAttachment(Owner->GetRootComponent());
			BobberMesh->RegisterComponent();
			const FBox Box = Mesh->GetBoundingBox();
			BobberBottom = FMath::Max(0.f, static_cast<float>(-Box.Min.Z)) * Row.BobberScale;
			BobberTop = FMath::Max(0.f, static_cast<float>(Box.Max.Z)) * Row.BobberScale;
		}
	}

	if (!Line && !bLineTried)
	{
		bLineTried = true;
		UStaticMesh* Mesh = LureFishingPrivate::LoadIfExists(Settings->LineMesh);
		if (!Mesh)
		{
			UE_LOG(LogLureFishing, Warning, TEXT("Line mesh '%s' is missing; no fishing line shown."), *Settings->LineMesh.ToString());
			return;
		}
		Line = NewObject<ULureFishingLineComponent>(Owner, TEXT("FishingLine"), RF_Transient);
		Line->SetupAttachment(Owner->GetRootComponent());
		Line->RegisterComponent();
		Line->Setup(Mesh, Settings->LoadLineMaterial(), Settings->LineColor, Row.LineSegments);
		// T-032: the line reads the drawn rod tip when it simulates (after the camera and arms moved), so it never lags the rod.
		Line->SetStartProvider(FLureLinePointProvider::CreateUObject(this, &ULureFishingComponent::GetLineStart));
	}
}

void ULureFishingComponent::ComputeBobberPose(double Now, FVector& OutLocation, FRotator& OutRotation) const
{
	OutRotation = FRotator::ZeroRotator;
	OutLocation = NetState.BobberRest;
	if (NetState.State == ELureFishingState::Idle)
	{
		return;
	}
	const FLureFishingRow& Row = GetProfile();
	const float Since = static_cast<float>(FMath::Max(0.0, Now - NetState.StateStartTime));

	if (NetState.State == ELureFishingState::Casting)
	{
		const FVector Rest = NetState.bOnWater ? FVector(NetState.BobberRest) : FVector(NetState.BobberRest) + FVector::UpVector * BobberBottom;
		const float Alpha = NetState.FlightTime > 0.f ? Since / NetState.FlightTime : 1.f;
		const float Arc = Row.CastArcHeightRatio * static_cast<float>(FVector::Dist2D(NetState.CastOrigin, NetState.BobberRest));
		OutLocation = FLureFishingRules::CastArcPoint(NetState.CastOrigin, Rest, Arc, Alpha);
		return;
	}
	if (!NetState.bOnWater)
	{
		OutLocation = FVector(NetState.BobberRest) + FVector::UpVector * BobberBottom; // lies on the ground
		OutRotation = FRotator(0.f, 0.f, 80.f);
		return;
	}

	// Floating: a slow bob and wobble (red upright = waiting).
	const float Time = static_cast<float>(FMath::Fmod(Now, 3600.0));
	const float Omega = 2.f * UE_PI * Row.BobberBobFrequency;
	OutLocation.Z += Row.BobberBobAmplitude * FMath::Sin(Omega * Time);
	OutRotation.Roll = Row.BobberTiltDeg * FMath::Sin(0.7f * Omega * Time + 1.f);
	OutRotation.Pitch = 0.6f * Row.BobberTiltDeg * FMath::Sin(0.9f * Omega * Time);

	switch (NetState.State)
	{
	case ELureFishingState::Waiting:
	{
		// A nibble tips the bobber so its white half shows (B-S4).
		const float SinceNibble = static_cast<float>(Now - NetState.LastNibbleTime);
		if (NetState.NibbleId != 0 && SinceNibble >= 0.f && SinceNibble <= Row.NibbleDuration)
		{
			const float S = FMath::Sin(UE_PI * SinceNibble / FMath::Max(0.05f, Row.NibbleDuration));
			OutRotation.Pitch += Row.NibbleTiltDeg * S;
			OutLocation.Z -= 0.15f * Row.BiteDipDepth * S;
		}
		break;
	}
	case ELureFishingState::Biting:
	{
		// Pulled under (the red top disappears) and tugged while the hook window is open.
		const float Ramp = FMath::Clamp(Since / 0.06f, 0.f, 1.f);
		const float Tug = FMath::Sin(2.f * UE_PI * Row.BiteDipRate * Since);
		OutLocation.Z = NetState.BobberRest.Z - Row.BiteDipDepth * Ramp * (0.85f + 0.15f * Tug);
		OutLocation.X += 3.f * Tug;
		OutRotation.Pitch += 20.f * Tug;
		break;
	}
	case ELureFishingState::Hooked:
		OutLocation.Z = NetState.BobberRest.Z - Row.BiteDipDepth;
		if (FightNet.bActive && GetOwner())
		{
			// The bobber rides on the fish: LineOut from the player, swung by SideDeg, pulled under with the tension and dives.
			const FVector Player = GetOwner()->GetActorLocation();
			FVector Direction = (FVector(NetState.BobberRest) - Player).GetSafeNormal2D();
			if (Direction.IsNearlyZero())
			{
				Direction = GetOwner()->GetActorForwardVector().GetSafeNormal2D();
			}
			Direction = Direction.RotateAngleAxis(FightNet.SideDeg, FVector::UpVector);
			const FLureFishFightRow& Tuning = GetFightTuning();
			OutLocation = Player + Direction * FightNet.LineOut;
			OutLocation.Z = NetState.BobberRest.Z - Row.BiteDipDepth * (0.5f + 0.5f * FMath::Clamp(FightNet.GetTension01(), 0.f, 1.f))
				- Tuning.DiveBobberShare * FightNet.Depth;
			OutRotation = FRotator(FMath::Clamp(40.f * FightNet.GetTension01(), 0.f, 60.f), Direction.Rotation().Yaw, 0.f);
		}
		break;
	default:
		break;
	}
}

FVector ULureFishingComponent::GetBobberLocation() const
{
	FVector Location;
	FRotator Rotation;
	ComputeBobberPose(GetFishingTime(), Location, Rotation);
	return Location;
}

FVector ULureFishingComponent::GetLineStart() const
{
	const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
	if (RodMesh && RodMesh->IsVisible() && IsOwnerLocallyControlled() && RodMesh->DoesSocketExist(Settings->RodLineSocket))
	{
		// The rod is a first-person primitive (own FOV, scaled toward the eye): move the socket to where it is drawn.
		const FVector Tip = RodMesh->GetSocketLocation(Settings->RodLineSocket);
		if (UCameraComponent* Camera = GetOwnerCamera())
		{
			FMinimalViewInfo View;
			Camera->GetCameraView(0.f, View);
			return View.TransformWorldToFirstPerson(Tip, /*bIgnoreFirstPersonScale*/ false);
		}
		return Tip;
	}
	// Other players (no visible rod yet) and the fallback: an estimate from the eye, turned by the rod's aim in a fight (T-028).
	FRotator Aim = GetOwner() ? GetOwner()->GetActorRotation() : FRotator::ZeroRotator;
	if (const APawn* Pawn = GetPawn())
	{
		Aim = Pawn->GetBaseAimRotation();
	}
	const FRotator RodAimTurn = FLureRodControl::RodLook(RodAimVisual.Y, RodAimVisual.X, GetFightTuning());
	return GetEyeLocation() + FRotator(RodAimTurn.Pitch, Aim.Yaw + RodAimTurn.Yaw, 0.f).RotateVector(Settings->RodTipOffsetFromEye);
}

void ULureFishingComponent::GetViewer(FVector& OutLocation, float& OutFovDeg) const
{
	OutLocation = GetEyeLocation();
	OutFovDeg = 90.f;
	const UWorld* World = GetWorld();
	const APlayerController* Local = World ? World->GetFirstPlayerController() : nullptr;
	if (Local && Local->IsLocalController() && Local->PlayerCameraManager)
	{
		const FMinimalViewInfo& View = Local->PlayerCameraManager->GetCameraCacheView();
		OutLocation = View.Location;
		OutFovDeg = View.FOV;
		if (IsOwnerLocallyControlled())
		{
			if (UCameraComponent* Camera = GetOwnerCamera())
			{
				OutLocation = Camera->GetComponentLocation(); // this frame's eye (the cache is last frame's)
			}
		}
	}
}

// ---- HUD text ----

FString ULureFishingComponent::GetStatusText() const
{
	using namespace LureFishingPrivate;
	TArray<FString> Lines;
	const double Now = GetFishingTime();
	const float MessageSeconds = GetDefault<ULureFishingSettings>()->HudMessageSeconds;

	if (bCharging)
	{
		Lines.Add(FString::Printf(TEXT("Cast power %d%%  (release to cast)"), FMath::RoundToInt(Charge * 100.f)));
	}
	switch (NetState.State)
	{
	case ELureFishingState::Waiting:
		if (!NetState.bOnWater)
		{
			Lines.Add(TEXT("The bobber landed on land. Click/RT to reel in."));
		}
		else
		{
			// T-027 placeholder lines: the hot spot, which water this is, and why nothing bites (if so).
			if (!NetState.Water.HotSpotType.IsNone())
			{
				Lines.Add(FLureWaterQuery::HotSpotHudText(NetState.Water.HotSpotType));
			}
			Lines.Add(FLureWaterRules::WaterLine(FLureWaterQuery::GetAreaDisplayName(GetWorld(), NetState.SpotId)));
			if (NetState.bNoFishHere && (FLureWaterRules::ShowsAtOnce(NetState.Water.NoBiteReason) || Now - NetState.StateStartTime >= GetProfile().NoBiteHintDelay))
			{
				const FString Reason = FLureWaterRules::NoBiteText(NetState.Water.NoBiteReason, MakeEnvironment().BaitTag);
				if (!Reason.IsEmpty())
				{
					Lines.Add(Reason);
				}
			}
		}
		break;
	case ELureFishingState::Biting:
		Lines.Add(TEXT("BITE! Click/RT to hook!"));
		break;
	case ELureFishingState::Hooked:
		Lines.Add(FString::Printf(TEXT("Hooked: %s"), *FishLabel(HookedFish)));
		if (FightNet.bActive)
		{
			// Placeholder fight readout (plain text; Jimmy directs the real UI later).
			GetFightTuning(); // resolves the fight tables on this machine (the move labels live in DT_FightPattern)
			FString FishState = FightNet.bExhausted ? TEXT("tired") : FightNet.MoveId.ToString();
			if (!FightNet.bExhausted)
			{
				if (const UDataTable* Patterns = PatternTableRef.Get(); Patterns && !FightNet.PatternId.IsNone()
					&& Patterns->GetRowStruct() && Patterns->GetRowStruct()->IsChildOf(FLureFightPatternRow::StaticStruct()))
				{
					if (const FLureFightPatternRow* Pattern = reinterpret_cast<const FLureFightPatternRow*>(Patterns->FindRowUnchecked(FightNet.PatternId)))
					{
						const int32 MoveIndex = Pattern->FindMove(FightNet.MoveId);
						if (MoveIndex != INDEX_NONE && !Pattern->Moves[MoveIndex].Label.IsEmpty())
						{
							FishState = Pattern->Moves[MoveIndex].Label.ToString();
						}
					}
				}
			}
			const float Tension01 = FightNet.GetTension01();
			Lines.Add(FString::Printf(TEXT("Fish: %s   stamina %d%%"), *FishState, FMath::RoundToInt(FightNet.Stamina * 100.f)));
			Lines.Add(FString::Printf(TEXT("Tension %s %d%%"), *TensionBar(Tension01), FMath::RoundToInt(Tension01 * 100.f)));
			Lines.Add(FString::Printf(TEXT("Line out %.1f m of %.0f m"), FightNet.LineOut / 100.f, FightNet.SpoolLength / 100.f));
			AppendRodHudLines(Lines); // T-028: "Rod: back-right", "Reel 2/3", "Fish runs LEFT: pull right"
			const bool bReelingNow = FightNet.bReeling || WantsToReel();
			Lines.Add(bReelingNow ? TEXT("Reeling. Release to let it run.") : TEXT("Hold Click/RT to reel."));
			if (FightNet.SnapProgress > 0.f)
			{
				Lines.Add(TEXT("!! The line is about to snap: ease off !!"));
			}
			else if (!bReelingNow && FightNet.SlackProgress > 0.4f) // T-028b: one slack rule: reeling is never slack (FLureFight::IsSlack)
			{
				Lines.Add(TEXT("!! Slack line: reel or it throws the hook !!"));
			}
		}
		break;
	default:
		break;
	}

	// T-032c: a lost fish's message stays longer (FightEndMessageSeconds) so a player still running sees why the fight ended.
	const bool bFightEnd = NetState.LastResult == ELureFishingResult::Lost || NetState.LastResult == ELureFishingResult::Snapped
		|| NetState.LastResult == ELureFishingResult::ThrewHook;
	const float ResultSeconds = bFightEnd ? FMath::Max(MessageSeconds, GetDefault<ULureFishingSettings>()->FightEndMessageSeconds) : MessageSeconds;
	if (NetState.ResultId != 0 && Now - NetState.ResultTime <= ResultSeconds)
	{
		const FString Reason = ReasonText(NetState.ResultReason);
		switch (NetState.LastResult)
		{
		case ELureFishingResult::Missed:
			Lines.Add(TEXT("Missed! The fish got away."));
			break;
		case ELureFishingResult::Landed:
			Lines.Add(FString::Printf(TEXT("Caught: %s"), *FishLabel(LastLandedFish)));
			break;
		case ELureFishingResult::Lost:
			Lines.Add(FString::Printf(TEXT("The fish got away (%s)."), Reason.IsEmpty() ? TEXT("line reeled in") : *Reason));
			break;
		case ELureFishingResult::ReeledIn:
			if (!Reason.IsEmpty())
			{
				Lines.Add(FString::Printf(TEXT("Line reeled in (%s)."), *Reason));
			}
			break;
		case ELureFishingResult::Refused:
			Lines.Add(FString::Printf(TEXT("Can't cast: %s."), *Reason));
			break;
		case ELureFishingResult::Spooked:
			Lines.Add(TEXT("Too early! You scared the fish."));
			break;
		case ELureFishingResult::Snapped:
			Lines.Add(FightNet.Outcome == ELureFightOutcome::Spooled ? TEXT("The fish took all your line. It snapped!") : TEXT("SNAP! The line broke. The fish got away."));
			break;
		case ELureFishingResult::ThrewHook:
			Lines.Add(TEXT("The fish threw the hook! Keep the line tight."));
			break;
		default:
			break;
		}
	}
	if (LocalRefusal != ELureCastBlock::None && GetLocalTime() - LocalRefusalTime <= MessageSeconds)
	{
		Lines.Add(FString::Printf(TEXT("Can't cast: %s."), ReasonText(LocalRefusal)));
	}
	return FString::Join(Lines, TEXT("\n"));
}

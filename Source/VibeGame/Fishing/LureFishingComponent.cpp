// Lure: casting, bobber, bite and hook (T-006).

#include "Fishing/LureFishingComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
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
#include "Fish/FishSettings.h"
#include "Fishing/FishingSpots.h"
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
	Conditions.bHasRod = bRodEquipped;
	Conditions.bSwimming = Movement && Movement->IsSwimming();
	Conditions.bFalling = Movement && Movement->IsFalling();
	Conditions.Speed2D = Movement ? static_cast<float>(Movement->Velocity.Size2D()) : 0.f;
	Conditions.bLineOut = IsLineOut();
	return Conditions;
}

bool ULureFishingComponent::IsRodInHand() const
{
	const ACharacter* Character = GetCharacter();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	return bRodEquipped && !(Movement && Movement->IsSwimming());
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
	Environment.TimeOfDayHours = TimeOfDayOverride >= 0.f ? TimeOfDayOverride : Settings->DefaultTimeOfDayHours;
	Environment.BaitTag = BaitTag.IsValid() ? BaitTag : FGameplayTag::RequestGameplayTag(Settings->DefaultBait, /*ErrorIfNotFound*/ false);
	Environment.GearLuck = GearLuck;
	Environment.DefaultRegionTag = FGameplayTag::RequestGameplayTag(Settings->DefaultRegion, /*ErrorIfNotFound*/ false);
	Environment.OffSpotHabitatTag = FGameplayTag::RequestGameplayTag(Settings->OffSpotHabitat, /*ErrorIfNotFound*/ false);
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
	const float Yaw = FMath::IsFinite(AimYawDegrees) ? AimYawDegrees : static_cast<float>(Owner->GetActorRotation().Yaw);
	const FRotator Aim(0.f, Yaw, 0.f);

	// The line leaves about at the rod tip (kept out of walls right in front of the player).
	const FVector Eye = GetEyeLocation();
	FVector Origin = Eye + Aim.RotateVector(Settings->RodTipOffsetFromEye);
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LureCastOrigin), false, Owner);
		const FCollisionObjectQueryParams Objects(ECC_TO_BITFIELD(ECC_WorldStatic) | ECC_TO_BITFIELD(ECC_WorldDynamic));
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, Eye, Origin, Objects, Params))
		{
			Origin = Hit.Location - (Origin - Eye).GetSafeNormal() * 5.f;
		}
	}

	const float Distance = FLureFishingRules::CastDistance(Row, CastCharge);
	const FLureCastLanding Landing = FLureFishingSpots::ResolveLanding(World, Owner, Origin, FVector2D(Eye.X, Eye.Y),
		FVector2D(Aim.Vector().X, Aim.Vector().Y), Distance, *Settings);

	bHasSpot = Landing.bOnWater && FLureFishingSpots::FindSpotAt(World, Landing.Rest, Settings->FishingSpotTag, CurrentSpot);
	if (!bHasSpot)
	{
		CurrentSpot = FLureFishingSpot();
	}
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
	New.SpotId = bHasSpot ? CurrentSpot.SpotId : NAME_None;
	SetNetState(New);

	UE_LOG(LogLureFishing, Verbose, TEXT("%s casts %.0f cm (charge %.2f): %s%s."), *Owner->GetName(), Distance, CastCharge,
		Landing.bOnWater ? TEXT("water") : TEXT("land"), bHasSpot ? *FString::Printf(TEXT(", spot %s"), *CurrentSpot.SpotId.ToString()) : TEXT(""));
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
			NextBiteTime = FMath::Max(NextBiteTime, Now + Delay);
			// Nibbles still to come move with the bite.
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
	PendingFish = FFishInstance();
	HookedFish = FFishInstance();
	NextBiteTime = -1.0;
	NibbleSchedule.Reset();
	NextNibbleIndex = 0;

	FLureFishingNetState New = NetState;
	New.State = ELureFishingState::Idle;
	New.StateStartTime = Now;
	BeginResult(New, bHadFish ? ELureFishingResult::Lost : ELureFishingResult::ReeledIn, Reason, Now);
	SetNetState(New);
	OnFishingEvent.Broadcast(New.LastResult, Lost);
}

void ULureFishingComponent::ServerTick(double Now)
{
	if (NetState.State == ELureFishingState::Idle)
	{
		return;
	}

	// Rules that bring the line in: sprinting, swimming, crawling with a tucked rod, too far from the bobber.
	const float BobberDistance = GetOwner() ? static_cast<float>(FVector::Dist2D(GetOwner()->GetActorLocation(), NetState.BobberRest)) : 0.f;
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
		if (Row.AutoLandDelay > 0.f && Now >= NetState.StateStartTime + Row.AutoLandDelay)
		{
			LandFish(Now);
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
	const bool bCanBite = New.bOnWater && FLureFishingRules::CanHaveBites(bHasSpot ? &CurrentSpot : nullptr, MakeEnvironment());
	New.bNoFishHere = New.bOnWater && !bCanBite;
	SetNetState(New);
	if (bCanBite)
	{
		ScheduleBite(Now, /*bAfterMiss*/ false);
	}
}

void ULureFishingComponent::ScheduleBite(double Now, bool bAfterMiss)
{
	const FLureFishingRow& Row = GetProfile();
	const float Wait = FLureFishingRules::RandomBiteWait(Row, Rng, bAfterMiss);
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
		SetNetState(New);
		return;
	}

	const int32 Seed = static_cast<int32>(Rng.GetUnsignedInt());
	LastRollContext = FLureFishingRules::MakeRollContext(bHasSpot ? &CurrentSpot : nullptr, MakeEnvironment(), Seed);
	FFishInstance Fish;
	if (FLureFishingRules::DecideBite(Tables, LastRollContext, Fish))
	{
		PendingFish = Fish;
		New.State = ELureFishingState::Biting;
		New.StateStartTime = Now;
		New.bNoFishHere = false;
		SetNetState(New);
		UE_LOG(LogLureFishing, Verbose, TEXT("%s: bite (%s)."), *GetNameSafe(GetOwner()), *Fish.ToString());
		return;
	}

	// Nothing fits this spot, time or bait right now: say so, and look again later (the time of day moves on).
	New.bNoFishHere = true;
	SetNetState(New);
	NextBiteTime = Now + FMath::Max(1.f, GetProfile().BiteWaitMax);
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
	OnFishingEvent.Broadcast(ELureFishingResult::Hooked, HookedFish);
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
	UE_LOG(LogLureFishing, Log, TEXT("%s landed %s."), *GetNameSafe(GetOwner()), *LastLandedFish.ToString());
	OnFishingEvent.Broadcast(ELureFishingResult::Landed, LastLandedFish);
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
	}

	if (ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(GetOwner()))
	{
		Lure->SetHoldingRod(IsRodInHand());
	}

	if (GetNetMode() != NM_DedicatedServer)
	{
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
	UpdateRod();

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
			const FName Socket = GetDefault<ULureFishingSettings>()->BobberLineSocket;
			const FVector End = (BobberMesh && BobberMesh->DoesSocketExist(Socket)) ? BobberMesh->GetSocketLocation(Socket) : BobberLocation + FVector::UpVector * BobberTop;
			float Sag = Row->LineSag;
			if (NetState.State == ELureFishingState::Casting)
			{
				Sag *= 0.3f;
			}
			else if (NetState.State == ELureFishingState::Biting || NetState.State == ELureFishingState::Hooked)
			{
				Sag = 0.f; // taut: something pulls
			}
			FVector ViewLocation;
			float Fov = 90.f;
			GetViewer(ViewLocation, Fov);
			Line->SetLine(GetLineStart(), End, Sag, ViewLocation, Fov, Row->LinePixelWidth, GetDefault<ULureFishingSettings>()->LineReferenceScreenWidth, Row->LineMinWidth);
		}
		else
		{
			Line->Hide();
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

void ULureFishingComponent::UpdateRod()
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
	RodMesh->SetRelativeRotation(FRotator(Pitch, 0.f, 0.f));
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
		Line->Setup(Mesh, LureFishingPrivate::LoadIfExists(Settings->LineMaterial), Settings->LineColor, Row.LineSegments);
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
	// Other players (no visible rod yet) and the fallback: an estimate from the eye.
	FRotator Aim = GetOwner() ? GetOwner()->GetActorRotation() : FRotator::ZeroRotator;
	if (const APawn* Pawn = GetPawn())
	{
		Aim = Pawn->GetBaseAimRotation();
	}
	return GetEyeLocation() + FRotator(0.f, Aim.Yaw, 0.f).RotateVector(Settings->RodTipOffsetFromEye);
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
		else if (NetState.bNoFishHere && Now - NetState.StateStartTime >= GetProfile().NoBiteHintDelay)
		{
			Lines.Add(TEXT("Nothing is biting here. Click/RT to reel in."));
		}
		break;
	case ELureFishingState::Biting:
		Lines.Add(TEXT("BITE! Click/RT to hook!"));
		break;
	case ELureFishingState::Hooked:
		Lines.Add(FString::Printf(TEXT("Hooked: %s"), *FishLabel(HookedFish)));
		break;
	default:
		break;
	}

	if (NetState.ResultId != 0 && Now - NetState.ResultTime <= MessageSeconds)
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

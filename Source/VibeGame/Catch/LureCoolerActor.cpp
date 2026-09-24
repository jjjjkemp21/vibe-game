// Lure: the physical cooler (T-030).

#include "Catch/LureCoolerActor.h"
#include "Animation/AnimSequenceBase.h"
#include "Camera/CameraComponent.h"
#include "Catch/LureCatchSettings.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureFishItem.h"
#include "Catch/LureHandsComponent.h"
#include "Catch/LureSellCounter.h"
#include "Character/LurePlayerCharacter.h"
#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "Fish/FightFishVisual.h"
#include "Fish/FishAnimInstance.h"
#include "Fish/LureFightFishSubsystem.h"
#include "Fishing/FishingSpots.h"
#include "Fishing/LureFishingSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Misc/PackageName.h"
#include "Net/UnrealNetwork.h"
#include "Progression/LureCoolerComponent.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "LureCooler"

namespace LureCoolerPrivate
{
	/** Placeholder size (the starter model, ART pass 4b0fb7f: 44 x 66 x 39 cm): the body box and the lid slab. */
	const FVector PlaceholderBody(44.0f, 64.0f, 35.0f);
	const FVector PlaceholderLid(44.0f, 64.0f, 4.0f);

	const FName LidHingeSocket(TEXT("LidHinge"));
	const FName ContentsSocket(TEXT("Contents"));

	/**
	 *  A shown fish holds Pose at Time (T-030f). Through the fish anim class (ABP_Fish) when its graph has the Curled pin:
	 *  the ABP adds the clip onto the playing mesh's local-space ref pose, which is cook-safe for an additive clip. Otherwise
	 *  (no class, a native class, or no Curled pin) the old single-node player, which still shows the pose in the editor.
	 */
	void HoldPose(USkeletalMeshComponent& Skeletal, UClass* AnimClass, UAnimSequenceBase* Pose, float Time)
	{
		if (AnimClass && AnimClass->IsChildOf(UFishAnimInstance::StaticClass()))
		{
			Skeletal.SetAnimInstanceClass(AnimClass);
			UFishAnimInstance* Anim = Cast<UFishAnimInstance>(Skeletal.GetAnimInstance());
			if (Anim && Anim->CanPlayRole(EFishAnimRole::Curled) && Anim->SetHeldPose(Pose, Time))
			{
				return;
			}
			Skeletal.SetAnimInstanceClass(nullptr);
		}
		Skeletal.PlayAnimation(Pose, /*bLooping*/ false);
		Skeletal.SetPosition(Time, /*bFireNotifies*/ false);
		Skeletal.Stop();
	}

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
		return (!Package.IsEmpty() && FPackageName::DoesPackageExist(Package)) ? Ref.LoadSynchronous() : nullptr;
	}

	UStaticMesh* EngineCube()
	{
		static TWeakObjectPtr<UStaticMesh> Cube;
		if (!Cube.IsValid())
		{
			Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		}
		return Cube.Get();
	}

	/**
	 * The cooler's box: the body plus the closed lid (the lid is part of the cooler for carrying, placing and clearance).
	 * The lid sits on the body's LidHinge socket (scale not included, as RefreshLook attaches it), else on the back top edge.
	 */
	FBox CoolerBounds(const UStaticMesh* Body, const UStaticMesh* Lid)
	{
		const FBox BodyBox = Body->GetBoundingBox();
		if (!Lid)
		{
			return BodyBox;
		}
		FTransform Hinge(FVector(BodyBox.Min.X, 0.0, BodyBox.Max.Z));
		if (const UStaticMeshSocket* Socket = Body->FindSocket(LidHingeSocket))
		{
			Hinge = FTransform(Socket->RelativeRotation, Socket->RelativeLocation);
		}
		return BodyBox + Lid->GetBoundingBox().TransformBy(Hinge);
	}
}

ALureCoolerActor::ALureCoolerActor()
{
	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	CollisionBox->SetupAttachment(ItemRoot);
	CollisionBox->InitBoxExtent(BoxHalfExtent);
	CollisionBox->SetRelativeLocation(BoxCenter);
	CollisionBox->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	CollisionBox->SetCollisionResponseToChannel(FLureFishingSpots::CastChannel, ECR_Ignore); // never stops a cast or a dropped fish
	CollisionBox->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CollisionBox->CanCharacterStepUpOn = ECB_Yes;
	CollisionBox->SetCanEverAffectNavigation(false);
	CollisionBox->SetHiddenInGame(true);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(VisualRoot);
	BodyMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	BodyMesh->SetGenerateOverlapEvents(false);
	BodyMesh->SetCanEverAffectNavigation(false);

	LidPivot = CreateDefaultSubobject<USceneComponent>(TEXT("LidPivot"));
	LidPivot->SetupAttachment(VisualRoot);

	LidMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LidMesh"));
	LidMesh->SetupAttachment(LidPivot);
	LidMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	LidMesh->SetGenerateOverlapEvents(false);
	LidMesh->SetCanEverAffectNavigation(false);

	ContentsRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ContentsRoot"));
	ContentsRoot->SetupAttachment(VisualRoot);

	Storage = CreateDefaultSubobject<ULureCoolerComponent>(TEXT("Storage"));

	// Coolers are few and matter wherever they stand (a player's catch waits in them).
	bAlwaysRelevant = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		BodyMesh->SetStaticMesh(Cube.Object);
		LidMesh->SetStaticMesh(Cube.Object);
	}
}

void ALureCoolerActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ALureCoolerActor, bLidOpen);
	DOREPLIFETIME(ALureCoolerActor, LidPulseId);
	DOREPLIFETIME(ALureCoolerActor, OwningPlayerState);
	DOREPLIFETIME(ALureCoolerActor, CoolerGuid);
	DOREPLIFETIME(ALureCoolerActor, bStarter);
}

ALureCoolerActor* ALureCoolerActor::SpawnCooler(UWorld* World, FName CoolerId, const FTransform& Transform, APlayerState* OwnerState, const FGuid& Guid, bool bInStarter)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	UClass* Class = GetDefault<ULureCatchSettings>()->CoolerClass.LoadSynchronous();
	if (!Class || !Class->IsChildOf(ALureCoolerActor::StaticClass()))
	{
		Class = ALureCoolerActor::StaticClass();
	}
	ALureCoolerActor* Cooler = World->SpawnActorDeferred<ALureCoolerActor>(Class, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Cooler)
	{
		return nullptr;
	}
	if (ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(World))
	{
		bool bInjected = false;
		const UDataTable* Table = Subsystem->GetCoolerTableOverride(bInjected);
		if (bInjected)
		{
			Cooler->Storage->SetCoolerTable(Table);
		}
	}
	Cooler->Storage->RestoreState(CoolerId, {}, 0.0f); // the row (unknown = the default row) and its capacity
	Cooler->OwningPlayerState = OwnerState;
	Cooler->CoolerGuid = Guid.IsValid() ? Guid : FGuid::NewGuid();
	Cooler->bStarter = bInStarter;
	Cooler->FinishSpawning(Transform);
	return Cooler;
}

void ALureCoolerActor::BeginPlay()
{
	Super::BeginPlay();
	if (Storage)
	{
		Storage->OnCoolerChanged.AddUniqueDynamic(this, &ALureCoolerActor::OnStorageChanged);
		// Every machine reads the row (name, look, the carrier's speed): a table injected into this world (tests, tools)
		// applies to the replicated copies too, not only to the server's (SpawnCooler).
		if (const ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(this); Subsystem && !HasAuthority())
		{
			bool bInjected = false;
			const UDataTable* Table = Subsystem->GetCoolerTableOverride(bInjected);
			if (bInjected)
			{
				Storage->SetCoolerTable(Table);
			}
		}
	}
	RefreshLook();
	if (HasAuthority() && Storage)
	{
		Storage->SetDecayRate(bLidOpen ? GetRow().OpenDecayRate : GetRow().ClosedDecayRate);
	}
	ApplyCollision();
	LidPitch = bLidOpen ? ULureCatchSubsystem::GetTuningFor(this).LidOpenPitch : 0.0f;
	LastLidPulseId = LidPulseId;
	RefreshDisplay();
}

// ---- State ----

FName ALureCoolerActor::GetCoolerId() const
{
	return Storage ? Storage->GetCoolerId() : NAME_None;
}

const FCoolerRow& ALureCoolerActor::GetRow() const
{
	if (!bLookResolved || LookCoolerId != GetCoolerId())
	{
		const_cast<ALureCoolerActor*>(this)->RefreshLook();
	}
	return CachedRow;
}

float ALureCoolerActor::GetDecayRate() const
{
	return Storage ? Storage->GetDecayRate() : 0.0f;
}

int32 ALureCoolerActor::GetNumFish() const
{
	return Storage ? Storage->GetNumFish() : 0;
}

int32 ALureCoolerActor::GetCapacity() const
{
	return Storage ? Storage->GetCapacity() : 1;
}

bool ALureCoolerActor::IsFull() const
{
	return !Storage || Storage->IsFull();
}

FText ALureCoolerActor::GetItemName() const
{
	const FCoolerRow& Row = GetRow();
	return Row.DisplayName.IsEmpty() ? LOCTEXT("Cooler", "Cooler") : Row.DisplayName;
}

FString ALureCoolerActor::GetSummary() const
{
	return FString::Printf(TEXT("%s (%d/%d, %s)"), *GetItemName().ToString(), GetNumFish(), GetCapacity(), bLidOpen ? TEXT("open") : TEXT("closed"));
}

bool ALureCoolerActor::GetHoldPose(EFPArmsPose& OutPose) const
{
	OutPose = EFPArmsPose::CarryCooler;
	return true;
}

float ALureCoolerActor::GetCarrySpeedMultiplier() const
{
	const float Multiplier = GetRow().CarrySpeedMultiplier;
	return (FMath::IsFinite(Multiplier) && Multiplier > 0.0f) ? FMath::Min(Multiplier, FLureProgressionData::MaxCarrySpeedMultiplier) : 1.0f;
}

void ALureCoolerActor::OnStorageChanged(ULureCoolerComponent* Changed)
{
	RefreshLook();
	RefreshDisplay();
}

// ---- Server ----

bool ALureCoolerActor::AuthoritySetLidOpen(bool bOpen)
{
	if (!HasAuthority())
	{
		UE_LOG(LogLureCatch, Warning, TEXT("%s: AuthoritySetLidOpen is server-only; ignored on this client."), *GetName());
		return false;
	}
	if (!IsFree() && bOpen)
	{
		return false; // not while someone carries it
	}
	bLidOpen = bOpen;
	if (Storage)
	{
		Storage->SetDecayRate(bOpen ? GetRow().OpenDecayRate : GetRow().ClosedDecayRate);
	}
	ForceNetUpdate();
	OnRep_Lid();
	return true;
}

bool ALureCoolerActor::AuthorityPutFishIn(APawn* Pawn, ALureFishItem* Fish)
{
	ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn);
	if (!HasAuthority() || !Hands || !IsValid(Fish) || !Fish->IsHeldBy(Pawn, ELureHoldMode::Hand) || !IsFree() || !Storage || Storage->IsFull())
	{
		return false;
	}
	if (!Storage->AddFish(Fish->GetCatchNow())) // spoils at the cooler's speed from now on
	{
		return false;
	}
	UE_LOG(LogLureCatch, Log, TEXT("%s put %s in %s (%d/%d)."), *GetNameSafe(Pawn), *Fish->GetFish().SpeciesId.ToString(), *GetName(), GetNumFish(), GetCapacity());
	Hands->AuthorityReleaseHeld();
	Fish->Destroy();
	if (!bLidOpen)
	{
		LidPulseId = static_cast<uint8>(LidPulseId + 1); // the lid opens and shuts around it
		OnRep_Lid();
	}
	ForceNetUpdate();
	return true;
}

ALureFishItem* ALureCoolerActor::AuthorityTakeFishOut(APawn* Pawn)
{
	ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn);
	if (!HasAuthority() || !Hands || !IsFree() || !bLidOpen || !Storage || Hands->IsHoldingSomething() || Hands->GetHangingFish())
	{
		return nullptr;
	}
	FLureCaughtFish Record;
	if (!Storage->RemoveLastFish(Record))
	{
		return nullptr;
	}
	const FTransform Above(GetActorRotation(), GetActorLocation() + FVector::UpVector * (BoxHalfExtent.Z * 2.0f + 10.0f));
	ALureFishItem* Item = ALureFishItem::SpawnFish(GetWorld(), Record, Above);
	if (!Item || !Hands->AuthorityTakeInHand(Item))
	{
		if (Item)
		{
			Item->Destroy();
		}
		Storage->AddFish(Record); // back where it was (the top of the pile)
		return nullptr;
	}
	UE_LOG(LogLureCatch, Log, TEXT("%s took %s out of %s (%d/%d left)."), *GetNameSafe(Pawn), *Record.Fish.SpeciesId.ToString(), *GetName(), GetNumFish(), GetCapacity());
	return Item;
}

bool ALureCoolerActor::AuthorityPickUp(APawn* Pawn)
{
	ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn);
	if (!HasAuthority() || !Hands || !IsFree() || Hands->IsHoldingSomething() || Hands->GetHangingFish() || Hands->IsPawnProne() || !Hands->CanHoldItems())
	{
		return false; // empty hands, standing or crouching, on land
	}
	if (bLidOpen)
	{
		AuthoritySetLidOpen(false); // you can't carry it open
	}
	return Hands->AuthorityTakeInHand(this);
}

bool ALureCoolerActor::FindFloor(const FVector& Above, float MaxFall, FHitResult& OutHit, const AActor* IgnoreActor) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LureCoolerFloor), false, this);
	if (IgnoreActor)
	{
		Params.AddIgnoredActor(IgnoreActor);
	}
	return FLureFishingSpots::TraceCast(World, OutHit, Above, Above - FVector::UpVector * FMath::Max(1.0f, MaxFall), Params);
}

bool ALureCoolerActor::FindPutDownSpot(const APawn* Carrier, FTransform& OutTransform) const
{
	const UWorld* World = GetWorld();
	if (!World || !Carrier)
	{
		return false;
	}
	const FLureCatchRow& Tuning = ULureCatchSubsystem::GetTuningFor(this);
	const ULureFishingSettings* FishingSettings = GetDefault<ULureFishingSettings>();
	const float Yaw = static_cast<float>(Carrier->GetActorRotation().Yaw);
	const FVector Forward = FRotator(0.0f, Yaw, 0.0f).Vector();
	float CapsuleRadius = 34.0f;
	float CapsuleHalfHeight = 90.0f;
	float MaxStepUp = 45.0f;
	bool bOnGround = true;
	if (const ACharacter* Character = Cast<ACharacter>(Carrier))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			CapsuleRadius = Capsule->GetScaledCapsuleRadius();
			CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		}
		if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			MaxStepUp = Movement->MaxStepHeight;
			bOnGround = Movement->IsMovingOnGround();
		}
	}
	// Only the floor you stand on (a step up at most): an open cooler is looked into from above by a standing player, the
	// view its contents display is made for (SK_Fish.anim.md "Cooler display"), never from beside a counter or table top.
	// In the air (a jump, a climb) that is the ground below you, not your raised feet: a jump can't lift the limit to a table.
	constexpr float GroundBelowReach = 400.0f; // well past a jump's height (about 90 cm) or a drop off the dock
	double FeetZ = Carrier->GetActorLocation().Z - CapsuleHalfHeight;
	FHitResult Ground;
	if (!bOnGround && FindFloor(Carrier->GetActorLocation(), CapsuleHalfHeight + GroundBelowReach, Ground, Carrier))
	{
		FeetZ = FMath::Min(FeetZ, static_cast<double>(Ground.ImpactPoint.Z));
	}
	const double MaxFloorZ = FeetZ + MaxStepUp + 5.0;
	// Face the player (the front and latch toward them); the box's depth along the view is its X half size.
	const FRotator Rotation(0.0f, Yaw + 180.0f, 0.0f);
	const float MinDistance = CapsuleRadius + BoxHalfExtent.X + 4.0f;
	TArray<float> Distances = { Tuning.PutDownDistance, Tuning.PutDownDistance * 0.85f, MinDistance };
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LureCoolerRoom), false, this);
	const FCollisionShape Box = FCollisionShape::MakeBox(BoxHalfExtent - FVector(1.0f));
	for (const float Distance : Distances)
	{
		if (!(Distance >= MinDistance - 0.01f))
		{
			continue;
		}
		const FVector Point = Carrier->GetActorLocation() + Forward * Distance;
		FHitResult Floor;
		if (!FindFloor(Point + FVector::UpVector * 50.0f, 50.0f + CapsuleHalfHeight + Tuning.PutDownMaxFall, Floor, Carrier))
		{
			continue;
		}
		if (Floor.ImpactNormal.Z < 0.7)
		{
			continue; // not walkable (too steep)
		}
		if (Floor.ImpactPoint.Z > MaxFloorZ)
		{
			continue; // a raised top (counter, table, crate), not the floor
		}
		if (ALureSellCounter::FindCounterAt(World, Floor.ImpactPoint))
		{
			continue; // a sell counter's top is for the fish on sale
		}
		float WaterZ = 0.0f;
		if (FLureFishingSpots::FindWaterSurfaceZ(World, FVector2D(Floor.ImpactPoint.X, Floor.ImpactPoint.Y), *FishingSettings, WaterZ)
			&& Floor.ImpactPoint.Z <= WaterZ + FishingSettings->LandTolerance)
		{
			continue; // in the water
		}
		const FQuat Quat = Rotation.Quaternion();
		const FVector Center = Floor.ImpactPoint + Quat.RotateVector(BoxCenter) + FVector::UpVector * 2.0f;
		if (World->OverlapBlockingTestByChannel(Center, Quat, ECC_WorldDynamic, Box, Params))
		{
			continue; // no room (a wall, a crate, a player)
		}
		OutTransform = FTransform(Rotation, Floor.ImpactPoint);
		return true;
	}
	return false;
}

bool ALureCoolerActor::AuthorityPutDown(FText* OutProblem)
{
	APawn* Carrier = GetHolder();
	if (!HasAuthority() || !Carrier || GetHoldMode() != ELureHoldMode::Hand)
	{
		return false;
	}
	FTransform Spot;
	if (!FindPutDownSpot(Carrier, Spot))
	{
		if (OutProblem)
		{
			*OutProblem = LOCTEXT("NoRoom", "No room to put the cooler down here");
		}
		return false;
	}
	AuthorityPlace(Spot.GetLocation(), Spot.Rotator(), GetVisualTransform().GetLocation(), /*bAnimate*/ true);
	UE_LOG(LogLureCatch, Log, TEXT("%s put %s down at %s."), *GetNameSafe(Carrier), *GetName(), *Spot.GetLocation().ToCompactString());
	return true;
}

void ALureCoolerActor::AuthorityPutDownAt(const FVector& Spot, float Yaw)
{
	if (!HasAuthority())
	{
		return;
	}
	const APawn* Carrier = GetHolder();
	FHitResult Floor;
	const FVector Rest = FindFloor(Spot + FVector::UpVector * 50.0f, 50.0f + ULureCatchSubsystem::GetTuningFor(this).PutDownMaxFall, Floor, Carrier) ? FVector(Floor.ImpactPoint) : Spot;
	AuthorityPlace(Rest, FRotator(0.0f, Yaw, 0.0f), GetVisualTransform().GetLocation(), /*bAnimate*/ true);
	UE_LOG(LogLureCatch, Log, TEXT("%s: %s put down at %s (forced)."), *GetNameSafe(Carrier), *GetName(), *Rest.ToCompactString());
}

float ALureCoolerActor::GetYawFacing(const FVector& Spot, const AActor* Viewer)
{
	if (!Viewer)
	{
		return 0.0f;
	}
	const FVector ToViewer = Viewer->GetActorLocation() - Spot;
	if (ToViewer.SizeSquared2D() < FMath::Square(10.0))
	{
		return FRotator::NormalizeAxis(static_cast<float>(Viewer->GetActorRotation().Yaw) + 180.0f);
	}
	return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(ToViewer.Y, ToViewer.X)));
}

void ALureCoolerActor::AuthoritySetOwningPlayerState(APlayerState* InOwner)
{
	if (HasAuthority())
	{
		OwningPlayerState = InOwner;
		ForceNetUpdate();
	}
}

FLureCoolerSaveData ALureCoolerActor::GetSaveData() const
{
	FLureCoolerSaveData Data;
	Data.CoolerGuid = CoolerGuid;
	Data.CoolerId = GetCoolerId();
	Data.bLidOpen = bLidOpen;
	Data.bStarter = bStarter;
	Data.Location = GetActorLocation();
	Data.Rotation = FRotator(0.0f, GetActorRotation().Yaw, 0.0f);
	if (const APawn* Carrier = IsFree() ? nullptr : GetHolder())
	{
		// Carried at save time: it is saved standing where its carrier last stood on dry ground.
		const ULureHandsComponent* Hands = ULureHandsComponent::Get(Carrier);
		FVector Spot;
		Data.Location = (Hands && Hands->GetLastDryGround(Spot)) ? Spot : Carrier->GetActorLocation();
		Data.Rotation = FRotator(0.0f, GetYawFacing(Data.Location, Carrier), 0.0f);
	}
	const double Now = FLureFreshness::GetServerTime(this);
	if (Storage)
	{
		for (const FLureCaughtFish& Fish : Storage->GetFish())
		{
			FLureCaughtFish Saved = Fish;
			Saved.Freshness.ExposedSeconds = Fish.Freshness.GetExposure(Now);
			Saved.Freshness.AnchorTime = 0.0;
			Data.Fish.Add(Saved);
		}
	}
	return Data;
}

void ALureCoolerActor::AuthorityRestore(const FLureCoolerSaveData& Data)
{
	if (!HasAuthority() || !Storage)
	{
		return;
	}
	if (Data.CoolerGuid.IsValid())
	{
		CoolerGuid = Data.CoolerGuid;
	}
	bStarter = Data.bStarter;
	Storage->RestoreState(Data.CoolerId, Data.Fish, 0.0f);
	RefreshLook();
	bLidOpen = Data.bLidOpen;
	Storage->SetDecayRate(bLidOpen ? GetRow().OpenDecayRate : GetRow().ClosedDecayRate);
	AuthorityPlace(Data.Location, Data.Rotation, Data.Location, /*bAnimate*/ false);
	ForceNetUpdate();
	OnRep_Lid();
}

// ---- Interaction ----

FVector ALureCoolerActor::GetInteractionLocation() const
{
	if (!IsFree())
	{
		return Super::GetInteractionLocation();
	}
	return GetActorTransform().TransformPosition(BoxCenter);
}

bool ALureCoolerActor::CanInteract(const APawn* Pawn) const
{
	return Pawn && IsValid(this) && (IsFree() || IsHeldBy(Pawn, ELureHoldMode::Hand));
}

FLureInteraction ALureCoolerActor::GetInteraction(const APawn* Pawn, ELureInteractKey Key) const
{
	const ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn);
	if (!Hands || !CanInteract(Pawn))
	{
		return FLureInteraction();
	}
	if (IsHeldBy(Pawn, ELureHoldMode::Hand))
	{
		return FLureInteraction::Make(ELureInteractVerb::PutDownCooler, LOCTEXT("PutDown", "Put the cooler down"));
	}
	if (Hands->GetCarriedCooler() || Hands->GetHangingFish() || !Hands->CanHoldItems())
	{
		return FLureInteraction(); // both hands busy, a fish still on the hook (grab it first), or swimming
	}
	const FText Count = FText::Format(LOCTEXT("Count", "{0}/{1}"), FText::AsNumber(GetNumFish()), FText::AsNumber(GetCapacity()));
	if (const ALureFishItem* Fish = Hands->GetHeldFish())
	{
		if (Key != ELureInteractKey::Primary)
		{
			return FLureInteraction(); // F falls through to the fish in your hand (drop it)
		}
		return IsFull()
			? FLureInteraction::Info(FText::Format(LOCTEXT("Full", "Cooler full ({0})"), Count))
			: FLureInteraction::Make(ELureInteractVerb::PutFishInCooler, FText::Format(LOCTEXT("PutIn", "Put the {0} in the cooler ({1})"), Fish->GetItemName(), Count));
	}
	if (!bLidOpen)
	{
		if (Key == ELureInteractKey::Primary)
		{
			return FLureInteraction::Make(ELureInteractVerb::OpenCooler, FText::Format(LOCTEXT("Open", "Open the cooler ({0})"), Count));
		}
		return Hands->IsPawnProne() ? FLureInteraction::Info(LOCTEXT("StandToCarry", "Stand up to carry the cooler"))
			: FLureInteraction::Make(ELureInteractVerb::PickUpCooler, LOCTEXT("PickUp", "Pick up the cooler"));
	}
	if (Key == ELureInteractKey::Secondary || GetNumFish() == 0)
	{
		return FLureInteraction::Make(ELureInteractVerb::CloseCooler, FText::Format(LOCTEXT("Close", "Close the cooler ({0})"), Count));
	}
	FLureCaughtFish Top;
	Storage->GetFishAt(GetNumFish() - 1, Top);
	ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(this);
	const FText TopName = Subsystem ? Subsystem->GetSpeciesDisplayName(Top.Fish.SpeciesId) : FText::FromName(Top.Fish.SpeciesId);
	return FLureInteraction::Make(ELureInteractVerb::TakeFishFromCooler, FText::Format(LOCTEXT("TakeOut", "Take out the {0} ({1})"), TopName, Count));
}

bool ALureCoolerActor::PerformInteraction(APawn* Pawn, ELureInteractVerb Verb)
{
	ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn);
	if (!HasAuthority() || !Hands)
	{
		return false;
	}
	switch (Verb)
	{
	case ELureInteractVerb::OpenCooler:
		return AuthoritySetLidOpen(true);
	case ELureInteractVerb::CloseCooler:
		return AuthoritySetLidOpen(false);
	case ELureInteractVerb::PutFishInCooler:
		return AuthorityPutFishIn(Pawn, Hands->GetHeldFish());
	case ELureInteractVerb::TakeFishFromCooler:
		return AuthorityTakeFishOut(Pawn) != nullptr;
	case ELureInteractVerb::PickUpCooler:
		return AuthorityPickUp(Pawn);
	case ELureInteractVerb::PutDownCooler:
	{
		FText Problem;
		if (!IsHeldBy(Pawn, ELureHoldMode::Hand))
		{
			return false;
		}
		if (!AuthorityPutDown(&Problem))
		{
			Hands->ClientNotice(Problem.ToString());
			return false;
		}
		return true;
	}
	default:
		return false;
	}
}

// ---- Presentation ----

bool ALureCoolerActor::GetFirstPersonAttachment(const APawn* Holder, USceneComponent*& OutParent, FName& OutSocket, FTransform& OutRelative) const
{
	const ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(Holder);
	if (!Lure)
	{
		return false;
	}
	const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
	USceneComponent* Arms = Lure->GetFirstPersonArms();
	OutParent = (Arms && Lure->GetFirstPersonArms()->GetSkeletalMeshAsset()) ? Arms : static_cast<USceneComponent*>(Lure->GetFirstPersonCamera());
	OutSocket = NAME_None;
	OutRelative = FTransform(Settings->CarriedCoolerRotation, Settings->CarriedCoolerOffset);
	return OutParent != nullptr;
}

FTransform ALureCoolerActor::GetThirdPersonAttachment() const
{
	const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
	return FTransform(Settings->ThirdPersonCoolerRotation, Settings->ThirdPersonCoolerOffset);
}

void ALureCoolerActor::OnHoldChanged(const FLureItemHold& OldHold)
{
	ApplyCollision();
	RefreshDisplay();
}

void ALureCoolerActor::ApplyCollision()
{
	if (CollisionBox)
	{
		CollisionBox->SetCollisionEnabled(IsFree() ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
}

void ALureCoolerActor::OnRep_Lid()
{
	if (LidPulseId != LastLidPulseId)
	{
		LastLidPulseId = LidPulseId;
		LidPulseTimeLeft = FMath::Max(0.3f, 2.0f * ULureCatchSubsystem::GetTuningFor(this).LidOpenTime);
	}
	RefreshDisplay();
}

void ALureCoolerActor::RefreshLook()
{
	if (!Storage || !BodyMesh || !LidMesh || !LidPivot || !ContentsRoot)
	{
		return;
	}
	const FName Id = Storage->GetCoolerId();
	if (bLookResolved && Id == LookCoolerId)
	{
		return;
	}
	bLookResolved = true;
	LookCoolerId = Id;
	CachedRow = Storage->FindRowOrDefault();

	using namespace LureCoolerPrivate;
	UStaticMesh* Body = IsRenderingMachine() ? LoadIfExists(CachedRow.BodyMesh) : nullptr;
	UStaticMesh* Lid = Body ? LoadIfExists(CachedRow.LidMesh) : nullptr;
	if (!Body && !IsRenderingMachine())
	{
		// A dedicated server still needs the box size: read the mesh bounds if the asset is there (no rendering).
		Body = LoadIfExists(CachedRow.BodyMesh);
		if (Body)
		{
			const FBox Bounds = CoolerBounds(Body, LoadIfExists(CachedRow.LidMesh));
			BoxHalfExtent = Bounds.GetExtent();
			BoxCenter = Bounds.GetCenter();
			Body = nullptr;
		}
	}
	bPlaceholderLook = Body == nullptr;
	if (Body)
	{
		BodyMesh->SetStaticMesh(Body);
		BodyMesh->SetRelativeTransform(FTransform::Identity);
		const FBox Bounds = Body->GetBoundingBox(); // the body alone: hinge, contents and lid-slab fallbacks
		const FBox Whole = CoolerBounds(Body, Lid);
		BoxHalfExtent = Whole.GetExtent();
		BoxCenter = Whole.GetCenter();
		if (BodyMesh->DoesSocketExist(LidHingeSocket))
		{
			LidPivot->AttachToComponent(BodyMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, LidHingeSocket);
			LidPivot->SetRelativeLocation(FVector::ZeroVector);
		}
		else
		{
			LidPivot->AttachToComponent(VisualRoot, FAttachmentTransformRules::KeepRelativeTransform);
			LidPivot->SetRelativeLocation(FVector(Bounds.Min.X, 0.0, Bounds.Max.Z));
		}
		if (BodyMesh->DoesSocketExist(ContentsSocket))
		{
			ContentsRoot->AttachToComponent(BodyMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, ContentsSocket);
			ContentsRoot->SetRelativeTransform(FTransform::Identity);
		}
		else
		{
			ContentsRoot->AttachToComponent(VisualRoot, FAttachmentTransformRules::KeepRelativeTransform);
			ContentsRoot->SetRelativeTransform(FTransform(FVector(0.0, 0.0, Bounds.Min.Z + 5.0)));
		}
		if (Lid)
		{
			LidMesh->SetStaticMesh(Lid);
			LidMesh->SetRelativeTransform(FTransform::Identity);
		}
		else if (UStaticMesh* Cube = EngineCube())
		{
			// A real body without its lid: a slab over the top, hinged at the back edge.
			LidMesh->SetStaticMesh(Cube);
			LidMesh->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(Bounds.GetExtent().X, 0.0, 2.0), FVector(Bounds.GetSize().X, Bounds.GetSize().Y, 4.0) / 100.0));
		}
	}
	else
	{
		// Placeholder: an engine cube box and slab of the starter model's size (pivot = bottom center, hinge at the back top edge).
		if (UStaticMesh* Cube = EngineCube())
		{
			BodyMesh->SetStaticMesh(Cube);
			LidMesh->SetStaticMesh(Cube);
		}
		BodyMesh->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, PlaceholderBody.Z * 0.5), PlaceholderBody / 100.0));
		LidPivot->AttachToComponent(VisualRoot, FAttachmentTransformRules::KeepRelativeTransform);
		LidPivot->SetRelativeLocation(FVector(-PlaceholderBody.X * 0.5, 0.0, PlaceholderBody.Z));
		LidMesh->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(PlaceholderLid.X * 0.5, 0.0, PlaceholderLid.Z * 0.5), PlaceholderLid / 100.0));
		ContentsRoot->AttachToComponent(VisualRoot, FAttachmentTransformRules::KeepRelativeTransform);
		ContentsRoot->SetRelativeTransform(FTransform(FVector(0.0, 0.0, 5.0)));
		if (IsRenderingMachine() || !LoadIfExists(CachedRow.BodyMesh))
		{
			BoxHalfExtent = FVector(PlaceholderBody.X * 0.5, PlaceholderBody.Y * 0.5, (PlaceholderBody.Z + PlaceholderLid.Z) * 0.5);
			BoxCenter = FVector(0.0, 0.0, BoxHalfExtent.Z);
		}
	}
	if (CollisionBox)
	{
		CollisionBox->SetBoxExtent(BoxHalfExtent);
		CollisionBox->SetRelativeLocation(BoxCenter);
	}
	DisplayKeys.Reset(); // the slots may have moved: rebuild the shown fish
}

void ALureCoolerActor::SetDisplayVisible(bool bVisible)
{
	bDisplayVisible = bVisible;
	for (UPrimitiveComponent* Fish : DisplayFish)
	{
		if (Fish)
		{
			Fish->SetVisibility(bVisible);
		}
	}
}

int32 ALureCoolerActor::GetNumDisplayedFish() const
{
	if (!bDisplayVisible)
	{
		return 0;
	}
	int32 Count = 0;
	for (const UPrimitiveComponent* Fish : DisplayFish)
	{
		Count += (Fish && Fish->IsVisible()) ? 1 : 0;
	}
	return Count;
}

void ALureCoolerActor::RefreshDisplay()
{
	if (!IsRenderingMachine() || !Storage || !ContentsRoot)
	{
		return;
	}
	ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(this);
	const FLureCoolerDisplayRow Row = Subsystem ? Subsystem->GetCoolerDisplayRow(GetCoolerId()) : FLureCoolerDisplayRow::GetFallbackRow();
	const TArray<FLureCaughtFish>& Fish = Storage->GetFish();
	const int32 Shown = FMath::Min(Fish.Num(), Row.Slots.Num());

	// Rebuild only when the shown fish change: the top ones of the pile are shown, slot 0 the lowest of them (taking one
	// out re-seats the rest), and a fish of the same species at another weight is another size.
	TArray<uint32> Wanted;
	for (int32 Index = 0; Index < Shown; ++Index)
	{
		const FFishInstance& Instance = Fish[Fish.Num() - Shown + Index].Fish;
		Wanted.Add(HashCombine(HashCombine(GetTypeHash(Instance.SpeciesId), GetTypeHash(Instance.Seed)), GetTypeHash(Instance.WeightKg)));
	}
	const bool bSame = Wanted == DisplayKeys && DisplayFish.Num() == Shown;
	if (!bSame)
	{
		for (UPrimitiveComponent* Old : DisplayFish)
		{
			if (Old)
			{
				Old->DestroyComponent();
			}
		}
		DisplayFish.Reset();
		DisplayKeys = Wanted;
		UAnimSequenceBase* Pose = LureCoolerPrivate::LoadIfExists(Row.FishPose);
		ULureFightFishSubsystem* Visuals = ULureFightFishSubsystem::Get(this);
		const FFishVisualRow VisualRow = Visuals ? Visuals->GetVisualRow() : FFishVisualRow::GetFallbackRow();
		UClass* AnimClass = (Visuals && Pose) ? Visuals->ResolveAnimClass() : nullptr;
		for (int32 Index = 0; Index < Shown; ++Index)
		{
			const FLureCaughtFish& Record = Fish[Fish.Num() - Shown + Index];
			const FLureCoolerDisplaySlot& Slot = Row.Slots[Index];
			UStreamableRenderAsset* Asset = Subsystem ? Subsystem->LoadSpeciesMesh(Record.Fish.SpeciesId) : nullptr;
			const float Reference = Subsystem ? Subsystem->GetSpeciesReferenceWeight(Record.Fish.SpeciesId) : 0.0f;
			// The fight fish's scale (T-030d: no size jump from hand to cooler), then the cooler's own fit cap below.
			const float WeightScale = FFightFishVisual::WeightScale(Record.Fish.WeightKg, Reference, VisualRow);
			const float Scale = FMath::Clamp(FMath::Min(WeightScale, Row.MaxFishScale), 0.05f, 10.0f);
			UPrimitiveComponent* Shown3D = nullptr;
			if (USkeletalMesh* Skeletal = Cast<USkeletalMesh>(Asset))
			{
				USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(this, NAME_None, RF_Transient);
				Component->SetSkeletalMeshAsset(Skeletal);
				Component->SetRelativeScale3D(FVector(Scale));
				Shown3D = Component;
			}
			else
			{
				UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
				UStaticMesh* Static = Cast<UStaticMesh>(Asset);
				const bool bPlaceholder = Static == nullptr;
				if (bPlaceholder)
				{
					Static = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
				}
				Component->SetStaticMesh(Static);
				Component->SetRelativeScale3D((bPlaceholder ? FVector(0.5f, 0.11f, 0.2f) : FVector::OneVector) * Scale);
				Shown3D = Component;
			}
			Shown3D->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			Shown3D->SetGenerateOverlapEvents(false);
			Shown3D->SetCanEverAffectNavigation(false);
			Shown3D->SetupAttachment(ContentsRoot);
			// The slot's X, Y and bed; the fish's origin lies its lie offset (at its shown size) above the bed.
			Shown3D->SetRelativeLocationAndRotation(Slot.Location + FVector(0.0f, 0.0f, Row.LieOffsetCm * Scale), Slot.Rotation);
			Shown3D->RegisterComponent();
			if (USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Shown3D); Skeletal && Pose)
			{
				LureCoolerPrivate::HoldPose(*Skeletal, AnimClass, Pose, Row.PoseTime);
			}
			if (bFirstPersonRendering)
			{
				Shown3D->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
			}
			DisplayFish.Add(Shown3D);
		}
	}
	SetDisplayVisible(bDisplayVisible);
}

void ALureCoolerActor::UpdatePresentation(float DeltaSeconds)
{
	if (!LidPivot)
	{
		return;
	}
	const FLureCatchRow& Tuning = ULureCatchSubsystem::GetTuningFor(this);
	float Target = bLidOpen ? Tuning.LidOpenPitch : 0.0f;
	if (LidPulseTimeLeft > 0.0f)
	{
		LidPulseTimeLeft -= DeltaSeconds;
		Target = Tuning.LidOpenPitch * 0.6f; // opens for the fish going in, then shuts again
	}
	const float Speed = Tuning.LidOpenPitch / FMath::Max(0.01f, Tuning.LidOpenTime);
	LidPitch = Tuning.LidOpenTime > 0.0f ? FMath::FInterpConstantTo(LidPitch, Target, DeltaSeconds, Speed) : Target;
	LidPivot->SetRelativeRotation(FRotator(LidPitch, 0.0f, 0.0f));
	const bool bShow = bLidOpen && LidPitch > 10.0f && IsFree();
	if (bShow != bDisplayVisible)
	{
		SetDisplayVisible(bShow);
	}
}

#undef LOCTEXT_NAMESPACE

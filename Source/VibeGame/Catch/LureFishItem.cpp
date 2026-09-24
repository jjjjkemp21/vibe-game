// Lure: a caught fish as a physical item (T-030).

#include "Catch/LureFishItem.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Catch/LureCatchSettings.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCoolerActor.h"
#include "Catch/LureHandsComponent.h"
#include "Catch/LureSellCounter.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "Fishing/FishingSpots.h"
#include "Fishing/LureFishingComponent.h"
#include "Fishing/LureFishingLineComponent.h"
#include "Fishing/LureFishingSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Net/UnrealNetwork.h"
#include "ReferenceSkeleton.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "LureFishItem"

FLureFishHookedChanged ALureFishItem::OnHookedChanged;
FLureFishVisualAdopted ALureFishItem::OnLandedVisualAdopted;

namespace LureFishItemPrivate
{
	/** Placeholder fish: the engine sphere squashed to about 50 x 11 x 20 cm (a Bonefish at its reference weight). */
	const FVector PlaceholderScale(0.5f, 0.11f, 0.2f);

	/** Component-space location of a bone in the mesh's reference pose (no pose evaluation needed). */
	bool GetRefBoneLocation(const USkeletalMesh* Mesh, FName Bone, FVector& OutLocation)
	{
		if (!Mesh)
		{
			return false;
		}
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		int32 Index = Ref.FindBoneIndex(Bone);
		if (Index == INDEX_NONE)
		{
			return false;
		}
		const TArray<FTransform>& Pose = Ref.GetRefBonePose();
		FTransform Transform = FTransform::Identity;
		while (Index != INDEX_NONE && Pose.IsValidIndex(Index))
		{
			Transform = Transform * Pose[Index];
			Index = Ref.GetParentIndex(Index);
		}
		OutLocation = Transform.GetLocation();
		return true;
	}

	UObject* LoadIfExists(const FSoftObjectPath& Path)
	{
		if (Path.IsNull())
		{
			return nullptr;
		}
		if (UObject* Loaded = Path.ResolveObject())
		{
			return Loaded;
		}
		const FString Package = Path.GetLongPackageName();
		return (!Package.IsEmpty() && FPackageName::DoesPackageExist(Package)) ? Path.TryLoad() : nullptr;
	}

	/** A standing cooler's footprint (plus Margin) holds Point (coolers ignore the cast channel, so drops fall through them) */
	bool IsInsideCooler(const ALureCoolerActor* Cooler, const FVector2D& Point, float Margin)
	{
		if (!IsValid(Cooler) || !Cooler->IsFree())
		{
			return false;
		}
		const FVector Local = Cooler->GetActorTransform().InverseTransformPositionNoScale(FVector(Point.X, Point.Y, Cooler->GetActorLocation().Z));
		const FVector Half = Cooler->GetBoxHalfExtent();
		const FVector Center = Cooler->GetBoxCenter();
		return FMath::Abs(Local.X - Center.X) <= Half.X + Margin && FMath::Abs(Local.Y - Center.Y) <= Half.Y + Margin;
	}

	/** Pulls a drop target back toward the thrower (along -Direction) until it is clear of every standing cooler (at most 150 cm) */
	FVector2D KeepClearOfCoolers(const UWorld* World, FVector2D Target, const FVector2D& Direction)
	{
		const ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(World);
		if (!Subsystem || Direction.IsNearlyZero())
		{
			return Target;
		}
		constexpr float Margin = 12.0f;
		constexpr float Step = 10.0f;
		for (int32 Steps = 0; Steps < 15; ++Steps)
		{
			bool bInside = false;
			for (const ALureCarryableItem* Item : Subsystem->GetItems())
			{
				bInside |= IsInsideCooler(Cast<ALureCoolerActor>(Item), Target, Margin);
			}
			if (!bInside)
			{
				break;
			}
			Target -= Direction * Step;
		}
		return Target;
	}
}

ALureFishItem::ALureFishItem()
{
	StaticFish = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticFish"));
	StaticFish->SetupAttachment(VisualRoot);
	StaticFish->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	StaticFish->SetGenerateOverlapEvents(false);
	StaticFish->SetCanEverAffectNavigation(false);

	SkeletalFish = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalFish"));
	SkeletalFish->SetupAttachment(VisualRoot);
	SkeletalFish->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SkeletalFish->SetGenerateOverlapEvents(false);
	SkeletalFish->SetCanEverAffectNavigation(false);
	SkeletalFish->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		StaticFish->SetStaticMesh(Sphere.Object);
		StaticFish->SetRelativeScale3D(LureFishItemPrivate::PlaceholderScale);
	}
}

void ALureFishItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ALureFishItem, Catch);
	DOREPLIFETIME(ALureFishItem, Counter);
}

ALureFishItem* ALureFishItem::SpawnFish(UWorld* World, const FLureCaughtFish& InCatch, const FTransform& Transform)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	UClass* Class = GetDefault<ULureCatchSettings>()->FishItemClass.LoadSynchronous();
	if (!Class || !Class->IsChildOf(ALureFishItem::StaticClass()))
	{
		Class = ALureFishItem::StaticClass();
	}
	ALureFishItem* Item = World->SpawnActorDeferred<ALureFishItem>(Class, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Item)
	{
		return nullptr;
	}
	Item->Catch = InCatch;
	Item->Catch.Freshness.SetRate(1.0f, FLureFreshness::GetServerTime(World)); // out of a cooler: spoils at rate 1
	Item->FinishSpawning(Transform);
	return Item;
}

void ALureFishItem::BeginPlay()
{
	Super::BeginPlay();
	EnsureLook();
}

void ALureFishItem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AdoptedVisual)
	{
		AdoptedVisual->Destroy(); // local (never replicated): it goes with the item
		AdoptedVisual = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

bool ALureFishItem::AdoptVisual(AActor* Visual)
{
	if (!IsValid(Visual) || Visual == this || !IsRenderingMachine() || !VisualRoot || AdoptedVisual)
	{
		return false;
	}
	const FTransform VisualWorld = Visual->GetActorTransform();
	AdoptedVisual = Visual; // first: EnsureLook must not claim another offer for this item
	EnsureLook(); // the mouth and grip points come from the species mesh (the same one the visual shows)
	const FVector MouthWorld = VisualWorld.GetLocation() + VisualWorld.GetRotation().RotateVector(GetMouthOffset());
	Visual->AttachToComponent(VisualRoot, FAttachmentTransformRules::KeepWorldTransform);
	Visual->SetActorRelativeLocation(FVector::ZeroVector);
	Visual->SetActorRelativeRotation(FRotator::ZeroRotator);
	StaticFish->SetVisibility(false);
	SkeletalFish->SetVisibility(false);
	// The swing starts where the landed fish is (the line end), then settles under the rod tip: now if it already hangs
	// (a client that got the item first), else when it is hooked (the server spawns the item, then hangs it).
	SwingStart = MouthWorld;
	bHasSwingStart = true;
	if (Hold.IsHeld() && Hold.Mode == ELureHoldMode::Hook)
	{
		Pendulum.Bob = SwingStart;
		Pendulum.Velocity = FVector::ZeroVector;
		Pendulum.bInitialized = true;
		bHasSwingStart = false;
	}
	if (bFirstPersonRendering)
	{
		bFirstPersonRendering = false;
		SetFirstPersonRendering(true); // include the visual's primitives
	}
	UE_LOG(LogLureCatch, Verbose, TEXT("%s adopted the landed fish visual %s."), *GetName(), *Visual->GetName());
	OnLandedVisualAdopted.Broadcast(this, VisualWorld);
	return true;
}

double ALureFishItem::GetNow() const
{
	return FLureFreshness::GetServerTime(this);
}

// ---- Record ----

void ALureFishItem::AuthoritySetCatch(const FLureCaughtFish& InCatch)
{
	if (!HasAuthority())
	{
		UE_LOG(LogLureCatch, Warning, TEXT("%s: AuthoritySetCatch is server-only; ignored on this client."), *GetName());
		return;
	}
	Catch = InCatch;
	Catch.Freshness.SetRate(1.0f, GetNow());
	ForceNetUpdate();
	EnsureLook();
}

FLureCaughtFish ALureFishItem::GetCatchNow() const
{
	FLureCaughtFish Now = Catch;
	Now.Freshness.SetRate(Catch.Freshness.Rate, GetNow());
	return Now;
}

void ALureFishItem::OnRep_Catch()
{
	EnsureLook();
}

float ALureFishItem::GetExposureSeconds() const
{
	return Catch.Freshness.GetExposure(GetNow());
}

float ALureFishItem::GetFreshness01() const
{
	ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(this);
	const FLureFreshnessRow Row = Subsystem ? Subsystem->GetFreshnessRow(Catch.Fish.SpeciesId) : FLureFreshnessRow::GetFallbackRow();
	return FLureFreshness::GetFreshness01(Row, GetExposureSeconds());
}

float ALureFishItem::GetValueShare() const
{
	ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(this);
	const FLureFreshnessRow Row = Subsystem ? Subsystem->GetFreshnessRow(Catch.Fish.SpeciesId) : FLureFreshnessRow::GetFallbackRow();
	return FLureFreshness::GetValueShare(Row, GetExposureSeconds());
}

int32 ALureFishItem::GetCurrentValue() const
{
	return FLureFreshness::GetCurrentValue(Catch.Fish, GetValueShare());
}

FText ALureFishItem::GetItemName() const
{
	ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(this);
	return Subsystem ? Subsystem->GetSpeciesDisplayName(Catch.Fish.SpeciesId) : FText::FromName(Catch.Fish.SpeciesId);
}

FString ALureFishItem::GetDescription() const
{
	ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(this);
	const FText Rarity = Subsystem ? Subsystem->GetRarityDisplayName(Catch.Fish.RarityId) : FText::FromName(Catch.Fish.RarityId);
	return FString::Printf(TEXT("%s (%s), %.2f kg, %d coins, fresh %d%%"), *GetItemName().ToString(), *Rarity.ToString(), Catch.Fish.WeightKg,
		GetCurrentValue(), FMath::RoundToInt(GetFreshness01() * 100.0f));
}

void ALureFishItem::AuthoritySetCounter(ALureSellCounter* InCounter)
{
	if (!HasAuthority() || Counter == InCounter)
	{
		return;
	}
	Counter = InCounter;
	ForceNetUpdate();
}

bool ALureFishItem::GetHoldPose(EFPArmsPose& OutPose) const
{
	OutPose = EFPArmsPose::HoldFish;
	return true;
}

// ---- Letting go ----

bool ALureFishItem::AuthorityDrop(APawn* Pawn, const FVector& Origin, const FVector2D& StartXY, const FVector2D& Direction2D, float Distance)
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World)
	{
		return false;
	}
	// Works from the hand, the hook or the ground: the flight starts where the fish is drawn now, then it lets go.
	const FVector From = GetVisualTransform().GetLocation();

	// Where it lands: tossed Distance ahead at Origin's height (a wall stops it just in front), clear of standing coolers,
	// then straight down onto the ground, or the water (the cast landing rules with no flight of their own). A cast-style
	// flight aimed at the water would hit the dock under the player's feet and drop the fish there.
	const FVector2D Direction = Direction2D.GetSafeNormal();
	FVector2D Target = StartXY + Direction * FMath::Max(0.0f, Distance);
	if (Distance > 0.0f)
	{
		FHitResult Wall;
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(LureFishDrop), false, Pawn);
		if (FLureFishingSpots::TraceCast(World, Wall, FVector(StartXY.X, StartXY.Y, Origin.Z), FVector(Target.X, Target.Y, Origin.Z), Params) && !Wall.bStartPenetrating)
		{
			const FVector2D WallXY(Wall.Location.X, Wall.Location.Y);
			Target = WallXY - Direction * FMath::Min(20.0f, static_cast<float>(FVector2D::Distance(StartXY, WallXY)));
		}
	}
	Target = LureFishItemPrivate::KeepClearOfCoolers(World, Target, Direction);
	const FLureCastLanding Landing = FLureFishingSpots::ResolveLanding(World, Pawn, FVector(Target.X, Target.Y, Origin.Z), Target, Direction, 0.0f,
		*GetDefault<ULureFishingSettings>());
	if (Landing.bOnWater)
	{
		AuthorityRelease(Pawn);
		return false;
	}
	const float Yaw = Pawn ? static_cast<float>(Pawn->GetViewRotation().Yaw) + 90.0f : static_cast<float>(GetActorRotation().Yaw);
	AuthorityPlace(Landing.Rest, FRotator(0.0f, Yaw, 0.0f), From, /*bAnimate*/ true); // lets go of the hold too
	AuthoritySetCounter(ALureSellCounter::FindCounterAt(World, Landing.Rest));
	UE_LOG(LogLureCatch, Log, TEXT("%s dropped %s at %s%s."), *GetNameSafe(Pawn), *Catch.Fish.SpeciesId.ToString(), *Landing.Rest.ToCompactString(),
		Counter ? *FString::Printf(TEXT(" (on %s)"), *Counter->GetName()) : TEXT(""));
	return true;
}

void ALureFishItem::AuthorityRelease(APawn* Pawn)
{
	if (!HasAuthority())
	{
		return;
	}
	UE_LOG(LogLureCatch, Log, TEXT("%s released %s into the water."), *GetNameSafe(Pawn), *Catch.Fish.SpeciesId.ToString());
	if (ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn))
	{
		Hands->ClientNotice(FText::Format(LOCTEXT("Released", "Released the {0}"), GetItemName()).ToString());
	}
	if (Hold.IsHeld())
	{
		AuthoritySetHold(nullptr, ELureHoldMode::None);
	}
	Destroy();
}

// ---- Interaction ----

FVector ALureFishItem::GetInteractionLocation() const
{
	return Super::GetInteractionLocation();
}

bool ALureFishItem::CanInteract(const APawn* Pawn) const
{
	if (!Pawn || !Catch.IsValid() || !IsValid(this))
	{
		return false;
	}
	if (Hold.IsHeld())
	{
		return Hold.Holder == Pawn; // your hook, your hand
	}
	return Counter == nullptr; // fish on a sell counter belong to the counter
}

FLureInteraction ALureFishItem::GetInteraction(const APawn* Pawn, ELureInteractKey Key) const
{
	const ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn);
	if (!Hands || !CanInteract(Pawn))
	{
		return FLureInteraction();
	}
	const FText Name = GetItemName();
	if (IsHeldBy(Pawn, ELureHoldMode::Hook))
	{
		if (Key == ELureInteractKey::Secondary)
		{
			return FLureInteraction::Make(ELureInteractVerb::ReleaseFish, FText::Format(LOCTEXT("LetGo", "Let the {0} go"), Name));
		}
		// Grab needs an empty hand (a full hand only happens through debug tools: the rod is stowed, so no new catch).
		return (Hands->IsHoldingSomething() || !Hands->CanHoldItems()) ? FLureInteraction()
			: FLureInteraction::Make(ELureInteractVerb::GrabFish, FText::Format(LOCTEXT("Grab", "Grab the {0}"), Name));
	}
	if (IsHeldBy(Pawn, ELureHoldMode::Hand))
	{
		return Key == ELureInteractKey::Secondary
			? FLureInteraction::Make(ELureInteractVerb::DropFish, FText::Format(LOCTEXT("Drop", "Drop the {0}"), Name))
			: FLureInteraction();
	}
	if (Key == ELureInteractKey::Primary && !Hands->IsHoldingSomething() && !Hands->GetHangingFish() && Hands->CanHoldItems())
	{
		return FLureInteraction::Make(ELureInteractVerb::GrabFish, FText::Format(LOCTEXT("PickUp", "Pick up the {0}"), Name));
	}
	return FLureInteraction();
}

bool ALureFishItem::PerformInteraction(APawn* Pawn, ELureInteractVerb Verb)
{
	ULureHandsComponent* Hands = ULureHandsComponent::Get(Pawn);
	if (!HasAuthority() || !Hands || !Pawn)
	{
		return false;
	}
	switch (Verb)
	{
	case ELureInteractVerb::GrabFish:
		return Hands->AuthorityTakeInHand(this);

	case ELureInteractVerb::ReleaseFish:
	{
		if (!IsHeldBy(Pawn, ELureHoldMode::Hook))
		{
			return false;
		}
		// It drops straight down from where it hangs (the server's estimate: below the hang pivot).
		const FVector Pivot = Hands->GetHangPivot();
		const FVector Below = Pivot - FVector::UpVector * ULureCatchSubsystem::GetTuningFor(this).HangLineLength;
		AuthorityDrop(Pawn, Below, FVector2D(Below.X, Below.Y), FVector2D(Pawn->GetActorForwardVector()), 0.0f);
		return true;
	}

	case ELureInteractVerb::DropFish:
	{
		if (!IsHeldBy(Pawn, ELureHoldMode::Hand))
		{
			return false;
		}
		const FVector Eye = Pawn->GetPawnViewLocation();
		const FVector2D Direction(FRotator(0.0f, Pawn->GetViewRotation().Yaw, 0.0f).Vector());
		AuthorityDrop(Pawn, Eye - FVector::UpVector * 25.0f, FVector2D(Eye.X, Eye.Y), Direction, ULureCatchSubsystem::GetTuningFor(this).DropForward);
		return true;
	}

	default:
		return false;
	}
}

// ---- Look ----

UPrimitiveComponent* ALureFishItem::GetFishMesh() const
{
	if (SkeletalFish && SkeletalFish->GetSkeletalMeshAsset() && SkeletalFish->IsVisible())
	{
		return SkeletalFish;
	}
	return StaticFish;
}

void ALureFishItem::EnsureLook()
{
	if (!IsRenderingMachine() || !Catch.IsValid() || !StaticFish || !SkeletalFish)
	{
		return;
	}
	ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(this);
	const bool bClaim = !AdoptedVisual && !bLookReady;
	if (!bLookReady || LookSpecies != Catch.Fish.SpeciesId)
	{
		bLookReady = true;
		LookSpecies = Catch.Fish.SpeciesId;
		UStreamableRenderAsset* Asset = Subsystem ? Subsystem->LoadSpeciesMesh(LookSpecies) : nullptr;
		if (USkeletalMesh* Skeletal = Cast<USkeletalMesh>(Asset))
		{
			SkeletalFish->SetSkeletalMeshAsset(Skeletal);
			SkeletalFish->SetVisibility(true);
			StaticFish->SetVisibility(false);
			const FBox Bounds = Skeletal->GetImportedBounds().GetBox();
			FVector Mouth;
			MouthOffset = LureFishItemPrivate::GetRefBoneLocation(Skeletal, TEXT("Mouth"), Mouth) ? Mouth : FVector(Bounds.Max.X, 0.0, Bounds.GetCenter().Z);
			FVector Grip;
			GripOffset = LureFishItemPrivate::GetRefBoneLocation(Skeletal, TEXT("Grip"), Grip) ? Grip : Bounds.GetCenter();
			LieHeight = FMath::Max(1.0f, static_cast<float>(Bounds.Max.Y));
		}
		else if (UStaticMesh* Static = Cast<UStaticMesh>(Asset))
		{
			StaticFish->SetStaticMesh(Static);
			StaticFish->SetVisibility(true);
			SkeletalFish->SetVisibility(false);
			const FBox Bounds = Static->GetBoundingBox();
			const UStaticMeshSocket* MouthSocket = Static->FindSocket(TEXT("Mouth"));
			const UStaticMeshSocket* GripSocket = Static->FindSocket(TEXT("Grip"));
			MouthOffset = MouthSocket ? MouthSocket->RelativeLocation : FVector(Bounds.Max.X, 0.0, Bounds.GetCenter().Z);
			GripOffset = GripSocket ? GripSocket->RelativeLocation : Bounds.GetCenter();
			LieHeight = FMath::Max(1.0f, static_cast<float>(Bounds.Max.Y));
		}
		else
		{
			// Placeholder: the squashed sphere (set in the constructor), tinted fish-silver.
			StaticFish->SetVisibility(true);
			SkeletalFish->SetVisibility(false);
			MouthOffset = FVector(50.0f * LureFishItemPrivate::PlaceholderScale.X, 0.0f, 0.0f);
			GripOffset = FVector(5.0f, 0.0f, 0.0f);
			LieHeight = 50.0f * LureFishItemPrivate::PlaceholderScale.Y;
			if (UMaterialInterface* Base = StaticFish->GetMaterial(0))
			{
				if (UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(Base, this))
				{
					Tint->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.55f, 0.66f, 0.72f));
					StaticFish->SetMaterial(0, Tint);
				}
			}
		}
	}

	// Size: the meshes are modeled at the species' reference weight (fishkit rule): scale = (Weight / Reference)^(1/3).
	const float Reference = Subsystem ? Subsystem->GetSpeciesReferenceWeight(Catch.Fish.SpeciesId) : 0.0f;
	WeightScale = (Reference > 0.0f && Catch.Fish.WeightKg > 0.0f) ? FMath::Clamp(FMath::Pow(Catch.Fish.WeightKg / Reference, 1.0f / 3.0f), 0.25f, 4.0f) : 1.0f;
	const bool bPlaceholder = !SkeletalFish->IsVisible() && StaticFish->GetStaticMesh() && StaticFish->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Engine/"));
	StaticFish->SetRelativeScale3D((bPlaceholder ? LureFishItemPrivate::PlaceholderScale : FVector::OneVector) * WeightScale);
	SkeletalFish->SetRelativeScale3D(FVector(WeightScale));
	if (AdoptedVisual)
	{
		StaticFish->SetVisibility(false);
		SkeletalFish->SetVisibility(false);
	}
	RefreshPresentation(); // the hold and rest offsets depend on the size

	// T-029 seam: a landed fight fish offered before this item arrived takes over the look.
	if (bClaim && Subsystem)
	{
		if (AActor* Visual = Subsystem->ClaimLandedVisual(Catch.Fish))
		{
			AdoptVisual(Visual);
		}
	}
}

FTransform ALureFishItem::GetRestVisualTransform() const
{
	// Lying on its right side (roll +90 puts its +Y side down; SK_Fish.anim.md "On the dock"), resting on the ground point.
	return FTransform(FRotator(0.0f, 0.0f, 90.0f), FVector(0.0f, 0.0f, GetLieHeight()));
}

bool ALureFishItem::GetFirstPersonAttachment(const APawn* Holder, USceneComponent*& OutParent, FName& OutSocket, FTransform& OutRelative) const
{
	const ALurePlayerCharacter* Lure = Cast<ALurePlayerCharacter>(Holder);
	if (!Lure)
	{
		return false;
	}
	const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
	const FTransform GripToOrigin(-GetGripOffset());
	USkeletalMeshComponent* Arms = Lure->GetFirstPersonArms();
	if (Arms && Arms->GetSkeletalMeshAsset() && !Settings->HeldFishSocket.IsNone() && Arms->DoesSocketExist(Settings->HeldFishSocket))
	{
		OutParent = Arms;
		OutSocket = Settings->HeldFishSocket;
		OutRelative = GripToOrigin * FTransform(Settings->HeldFishRotation, Settings->HeldFishOffset);
		return true;
	}
	if (UCameraComponent* Camera = Lure->GetFirstPersonCamera())
	{
		OutParent = Camera;
		OutSocket = NAME_None;
		OutRelative = GripToOrigin * FTransform(Settings->HeldFishCameraRotation, Settings->HeldFishCameraOffset);
		return true;
	}
	return false;
}

FTransform ALureFishItem::GetThirdPersonAttachment() const
{
	const ULureCatchSettings* Settings = GetDefault<ULureCatchSettings>();
	return FTransform(-GetGripOffset()) * FTransform(Settings->ThirdPersonFishRotation, Settings->ThirdPersonFishOffset);
}

void ALureFishItem::OnHoldChanged(const FLureItemHold& OldHold)
{
	const bool bWasHooked = OldHold.IsHeld() && OldHold.Mode == ELureHoldMode::Hook;
	const bool bHooked = Hold.IsHeld() && Hold.Mode == ELureHoldMode::Hook;
	if (!bHooked)
	{
		Pendulum.bInitialized = false;
		bHasSwingStart = bHasSwingStart && !Hold.IsHeld(); // only for the first hang after spawning
		ExternalHangDriver.Reset(); // off the hook: the line (T-032) lets go, the next hang starts with the pendulum
		if (HangLine)
		{
			HangLine->Hide();
		}
	}
	else if (!bWasHooked)
	{
		// Starts hanging still below the pivot, or from where an adopted landed fish was (T-029 seam).
		Pendulum.bInitialized = bHasSwingStart;
		Pendulum.Bob = SwingStart;
		Pendulum.Velocity = FVector::ZeroVector;
		bHasSwingStart = false;
	}
	if (bHooked != bWasHooked)
	{
		OnHookedChanged.Broadcast(this, bHooked);
	}
}

void ALureFishItem::SetExternalHangDriver(UObject* Driver)
{
	ExternalHangDriver = Driver;
	if (Driver && HangLine)
	{
		HangLine->Hide(); // the driver draws its own line
	}
	if (!Driver)
	{
		Pendulum.bInitialized = false;
		RefreshPresentation(); // back to following the holder
	}
}

float ALureFishItem::GetHangLineLength() const
{
	return ULureCatchSubsystem::GetTuningFor(this).HangLineLength;
}

void ALureFishItem::EnsureHangLine()
{
	if (HangLine || !IsRenderingMachine())
	{
		return;
	}
	const ULureFishingSettings* Settings = GetDefault<ULureFishingSettings>();
	UStaticMesh* Mesh = Cast<UStaticMesh>(LureFishItemPrivate::LoadIfExists(Settings->LineMesh.ToSoftObjectPath()));
	if (!Mesh)
	{
		return;
	}
	HangLine = NewObject<ULureFishingLineComponent>(this, TEXT("HangLine"), RF_Transient);
	HangLine->SetupAttachment(ItemRoot);
	HangLine->RegisterComponent();
	HangLine->Setup(Mesh, Cast<UMaterialInterface>(LureFishItemPrivate::LoadIfExists(Settings->LineMaterial.ToSoftObjectPath())), Settings->LineColor, 4);
}

void ALureFishItem::GetViewer(FVector& OutLocation, float& OutFovDeg) const
{
	const APawn* Holder = GetHolder();
	OutLocation = Holder ? Holder->GetPawnViewLocation() : GetActorLocation();
	OutFovDeg = 90.0f;
	const UWorld* World = GetWorld();
	const APlayerController* Local = World ? World->GetFirstPlayerController() : nullptr;
	if (Local && Local->IsLocalController() && Local->PlayerCameraManager)
	{
		const FMinimalViewInfo& View = Local->PlayerCameraManager->GetCameraCacheView();
		OutLocation = View.Location;
		OutFovDeg = View.FOV;
	}
}

void ALureFishItem::UpdateHooked(float DeltaSeconds)
{
	APawn* Holder = GetHolder();
	if (!Holder)
	{
		return;
	}
	EnsureLook();
	if (ExternalHangDriver.IsValid())
	{
		return; // T-032 seam: the physics line moves this fish and draws the line
	}
	const ULureHandsComponent* Hands = ULureHandsComponent::Get(Holder);
	const FVector Pivot = Hands ? Hands->GetHangPivot() : Holder->GetPawnViewLocation();
	const FLureCatchRow& Tuning = ULureCatchSubsystem::GetTuningFor(this);
	const UWorld* World = GetWorld();
	Pendulum.Step(Pivot, Tuning.HangLineLength, World ? World->GetGravityZ() : -980.0f, Tuning.HangDamping, DeltaSeconds);

	FVector ViewLocation;
	float Fov = 90.0f;
	GetViewer(ViewLocation, Fov);

	// Nose (+X) up the line; its right side (+Y) turned toward whoever looks at it.
	FVector Up = (Pivot - Pendulum.Bob).GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		Up = FVector::UpVector;
	}
	FVector ToViewer = ViewLocation - Pendulum.Bob;
	ToViewer -= Up * FVector::DotProduct(ToViewer, Up);
	if (!ToViewer.Normalize())
	{
		ToViewer = Holder->GetActorRightVector();
	}
	const FRotator Rotation = FRotationMatrix::MakeFromXY(Up, ToViewer).Rotator();
	SetActorLocationAndRotation(Pendulum.Bob - Rotation.RotateVector(GetMouthOffset()), Rotation);

	EnsureHangLine();
	if (HangLine)
	{
		float PixelWidth = 2.5f;
		float MinWidth = 0.05f;
		if (const ULureFishingComponent* Fishing = Holder->FindComponentByClass<ULureFishingComponent>())
		{
			const FLureFishingRow& Row = Fishing->GetProfile();
			PixelWidth = Row.LinePixelWidth;
			MinWidth = Row.LineMinWidth;
		}
		HangLine->SetLine(Pivot, Pendulum.Bob, 0.0f, ViewLocation, Fov, PixelWidth, GetDefault<ULureFishingSettings>()->LineReferenceScreenWidth, MinWidth);
	}
}

#undef LOCTEXT_NAMESPACE

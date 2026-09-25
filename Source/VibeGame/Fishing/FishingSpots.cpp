// Lure: fishing spot markers and water lookups (T-006).

#include "Fishing/FishingSpots.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "Engine/HitResult.h"
#include "Engine/TriggerBase.h"
#include "Engine/World.h"
#include "Fishing/LureFishingSettings.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/Volume.h"
#include "GameplayTagsManager.h"

namespace LureFishingSpotsPrivate
{
	bool ParseFloat(const FString& Text, float& Out)
	{
		const FString Trimmed = Text.TrimStartAndEnd();
		if (Trimmed.IsEmpty() || !FCString::IsNumeric(*Trimmed))
		{
			return false;
		}
		Out = FCString::Atof(*Trimmed);
		return FMath::IsFinite(Out);
	}

	bool ParseRange(const FString& Text, float& OutMin, float& OutMax)
	{
		FString Left, Right;
		if (!Text.Split(TEXT("-"), &Left, &Right))
		{
			return false;
		}
		return ParseFloat(Left, OutMin) && ParseFloat(Right, OutMax);
	}

	FGameplayTag RequestTag(const FString& Text, const TCHAR* Key, TArray<FString>* OutProblems)
	{
		const FString Name = Text.TrimStartAndEnd();
		if (Name.IsEmpty())
		{
			return FGameplayTag();
		}
		const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*Name), /*ErrorIfNotFound*/ false);
		if (!Tag.IsValid() && OutProblems)
		{
			OutProblems->Add(FString::Printf(TEXT("%s=%s is not a registered gameplay tag (Config/Tags/*.ini)"), Key, *Name));
		}
		return Tag;
	}
}

bool FLureFishingSpots::ParseSpotTags(const TArray<FName>& Tags, FName SpotTag, FLureFishingSpot& OutSpot, TArray<FString>* OutProblems)
{
	using namespace LureFishingSpotsPrivate;
	OutSpot = FLureFishingSpot();

	bool bIsSpot = false;
	bool bHasRadius = false;
	for (const FName& TagName : Tags)
	{
		const FString Tag = TagName.ToString();
		if (TagName == SpotTag)
		{
			bIsSpot = true;
			continue;
		}
		FString Key, Value;
		if (!Tag.Split(TEXT("="), &Key, &Value))
		{
			continue;
		}
		Key = Key.TrimStartAndEnd();
		Value = Value.TrimStartAndEnd();
		auto Problem = [OutProblems, &Tag](const TCHAR* Why)
		{
			if (OutProblems)
			{
				OutProblems->Add(FString::Printf(TEXT("'%s': %s"), *Tag, Why));
			}
		};

		if (Key.Equals(TEXT("Spot"), ESearchCase::IgnoreCase))
		{
			OutSpot.SpotId = FName(*Value);
		}
		else if (Key.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
		{
			OutSpot.DisplayName = Value;
		}
		else if (Key.Equals(TEXT("Habitat"), ESearchCase::IgnoreCase))
		{
			OutSpot.HabitatTag = RequestTag(Value, TEXT("Habitat"), OutProblems);
		}
		else if (Key.Equals(TEXT("Region"), ESearchCase::IgnoreCase))
		{
			OutSpot.RegionTag = RequestTag(Value, TEXT("Region"), OutProblems);
		}
		else if (Key.Equals(TEXT("Radius"), ESearchCase::IgnoreCase))
		{
			float Radius = 0.f;
			if (ParseFloat(Value, Radius) && Radius > 0.f)
			{
				OutSpot.Radius = Radius;
				bHasRadius = true;
			}
			else
			{
				Problem(TEXT("Radius must be a number > 0"));
			}
		}
		else if (Key.Equals(TEXT("Luck"), ESearchCase::IgnoreCase))
		{
			float Luck = 0.f;
			if (ParseFloat(Value, Luck) && Luck >= 0.f)
			{
				OutSpot.Luck = Luck;
			}
			else
			{
				Problem(TEXT("Luck must be a number >= 0"));
			}
		}
		else if (Key.Equals(TEXT("Hours"), ESearchCase::IgnoreCase))
		{
			TArray<FString> Windows;
			Value.ParseIntoArray(Windows, TEXT(";"), /*CullEmpty*/ true);
			for (const FString& Window : Windows)
			{
				float Start = 0.f, End = 0.f;
				if (ParseRange(Window, Start, End))
				{
					FFishTimeWindow Parsed;
					Parsed.StartHour = Start;
					Parsed.EndHour = End;
					OutSpot.Hours.Add(Parsed);
				}
				else
				{
					Problem(TEXT("Hours must look like 20-5;6-9"));
				}
			}
		}
		else if (Key.Equals(TEXT("Levels"), ESearchCase::IgnoreCase))
		{
			float Min = 0.f, Max = 0.f;
			if (!Value.IsEmpty() && ParseRange(Value, Min, Max))
			{
				OutSpot.LevelMin = FMath::RoundToInt(Min);
				OutSpot.LevelMax = FMath::RoundToInt(Max);
			}
			else if (!Value.IsEmpty())
			{
				Problem(TEXT("Levels must look like 4-5"));
			}
		}
		else if (Key.Equals(TEXT("Danger"), ESearchCase::IgnoreCase))
		{
			OutSpot.Danger = FName(*Value);
		}
		else if (Key.Equals(TEXT("CastFrom"), ESearchCase::IgnoreCase))
		{
			TArray<FString> Parts;
			Value.ParseIntoArray(Parts, TEXT(","), /*CullEmpty*/ true);
			float X = 0.f, Y = 0.f, Z = 0.f;
			if (Parts.Num() == 3 && ParseFloat(Parts[0], X) && ParseFloat(Parts[1], Y) && ParseFloat(Parts[2], Z))
			{
				OutSpot.CastFrom = FVector(X, Y, Z);
			}
			else
			{
				Problem(TEXT("CastFrom must look like x,y,z"));
			}
		}
	}

	if (!bIsSpot)
	{
		return false;
	}
	if (!bHasRadius)
	{
		if (OutProblems)
		{
			OutProblems->Add(TEXT("no usable Radius= tag: the marker is not a fishing spot"));
		}
		return false;
	}
	return true;
}

TArray<FLureFishingSpot> FLureFishingSpots::GatherSpots(const UWorld* World, FName SpotTag)
{
	TArray<FLureFishingSpot> Spots;
	if (!World || SpotTag.IsNone())
	{
		return Spots;
	}
	for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || !Actor->ActorHasTag(SpotTag))
		{
			continue;
		}
		FLureFishingSpot Spot;
		TArray<FString> Problems;
		const bool bOk = ParseSpotTags(Actor->Tags, SpotTag, Spot, &Problems);
		if (Problems.Num() > 0)
		{
			UE_LOG(LogLureFishing, Verbose, TEXT("Fishing spot marker %s: %s"), *Actor->GetName(), *FString::Join(Problems, TEXT("; ")));
		}
		if (!bOk)
		{
			continue;
		}
		Spot.Location = Actor->GetActorLocation();
		Spot.Marker = Actor;
		if (Spot.SpotId.IsNone())
		{
			Spot.SpotId = Actor->GetFName();
		}
		Spots.Add(MoveTemp(Spot));
	}
	return Spots;
}

bool FLureFishingSpots::FindSpotAt(const UWorld* World, const FVector& Location, FName SpotTag, FLureFishingSpot& OutSpot)
{
	float BestRatio = TNumericLimits<float>::Max();
	bool bFound = false;
	for (FLureFishingSpot& Spot : GatherSpots(World, SpotTag))
	{
		const float Distance = static_cast<float>(FVector::Dist2D(Location, Spot.Location));
		if (Distance > Spot.Radius)
		{
			continue;
		}
		const float Ratio = Distance / Spot.Radius;
		if (Ratio < BestRatio)
		{
			BestRatio = Ratio;
			OutSpot = Spot;
			bFound = true;
		}
	}
	return bFound;
}

bool FLureFishingSpots::FindWaterSurfaceZ(const UWorld* World, const FVector2D& XY, const ULureFishingSettings& Settings, float& OutZ)
{
	if (World)
	{
		// 1. Water volumes (T-026 swimming uses the engine's water physics volumes).
		for (TActorIterator<APhysicsVolume> It(const_cast<UWorld*>(World)); It; ++It)
		{
			const APhysicsVolume* Volume = *It;
			if (!IsValid(Volume) || !Volume->bWaterVolume)
			{
				continue;
			}
			const FBox Bounds = Volume->GetComponentsBoundingBox(/*bNonColliding*/ true);
			if (!Bounds.IsValid || XY.X < Bounds.Min.X || XY.X > Bounds.Max.X || XY.Y < Bounds.Min.Y || XY.Y > Bounds.Max.Y)
			{
				continue;
			}
			if (Volume->EncompassesPoint(FVector(XY.X, XY.Y, Bounds.Max.Z - 1.0)))
			{
				OutZ = static_cast<float>(Bounds.Max.Z);
				return true;
			}
		}

		// 2. Tagged water surfaces (e.g. the greybox water planes, which have no collision).
		if (!Settings.WaterTag.IsNone())
		{
			bool bFound = false;
			float Best = -TNumericLimits<float>::Max();
			for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
			{
				const AActor* Actor = *It;
				if (!IsValid(Actor) || !Actor->ActorHasTag(Settings.WaterTag))
				{
					continue;
				}
				const FBox Bounds = Actor->GetComponentsBoundingBox(/*bNonColliding*/ true);
				if (Bounds.IsValid && XY.X >= Bounds.Min.X && XY.X <= Bounds.Max.X && XY.Y >= Bounds.Min.Y && XY.Y <= Bounds.Max.Y)
				{
					Best = FMath::Max(Best, static_cast<float>(Bounds.Max.Z));
					bFound = true;
				}
			}
			if (bFound)
			{
				OutZ = Best;
				return true;
			}
		}
	}

	// 3. The sea level of the layouts.
	if (Settings.bUseFallbackWaterZ)
	{
		OutZ = Settings.FallbackWaterZ;
		return true;
	}
	return false;
}

bool FLureFishingSpots::BlocksCast(const FHitResult& Hit)
{
	const UPrimitiveComponent* Component = Hit.GetComponent();
	if (!Component)
	{
		return false;
	}
	// Design zones (shark_zone, shadow_zone...) are TriggerBoxes / volumes: never solid for a cast, whatever their profile.
	const AActor* Actor = Hit.GetActor();
	if (Actor && (Actor->IsA<AVolume>() || Actor->IsA<ATriggerBase>() || Actor->IsA<APawn>()))
	{
		return false;
	}
	if (Component->GetCollisionResponseToChannel(CastChannel) != ECR_Block)
	{
		return false;
	}
	// Overlap-only components (a trigger shape on any actor, OverlapAll decor) block nothing physical: casts pass them.
	for (const ECollisionChannel Physical : { ECC_WorldStatic, ECC_WorldDynamic, ECC_Pawn, ECC_PhysicsBody, ECC_Vehicle })
	{
		if (Component->GetCollisionResponseToChannel(Physical) == ECR_Block)
		{
			return true;
		}
	}
	return false;
}

bool FLureFishingSpots::TraceCast(const UWorld* World, FHitResult& OutHit, const FVector& Start, const FVector& End, const FCollisionQueryParams& Params)
{
	OutHit = FHitResult();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Query = Params;
	// Each pass ignores one more non-solid component; a few zones stacked on a line are the realistic worst case.
	constexpr int32 MaxPasses = 16;
	for (int32 Pass = 0; Pass < MaxPasses; ++Pass)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Start, End, CastChannel, Query))
		{
			return false;
		}
		if (BlocksCast(Hit))
		{
			OutHit = Hit;
			return true;
		}
		if (UPrimitiveComponent* Component = Hit.GetComponent())
		{
			Query.AddIgnoredComponent(Component);
		}
		else if (const AActor* Actor = Hit.GetActor())
		{
			Query.AddIgnoredActor(Actor);
		}
		else
		{
			return false;
		}
	}
	return false;
}

FLureCastLanding FLureFishingSpots::ResolveLanding(const UWorld* World, const AActor* IgnoreActor, const FVector& Origin, const FVector2D& StartXY,
	const FVector2D& Direction, float Distance, const ULureFishingSettings& Settings, const FLureFishingRow* Profile)
{
	FLureCastLanding Landing;
	const FVector2D Dir = Direction.GetSafeNormal();
	FVector2D TargetXY = StartXY + Dir * FMath::Max(0.f, Distance);

	float WaterZ = 0.f;
	Landing.bFoundWater = FindWaterSurfaceZ(World, TargetXY, Settings, WaterZ);
	Landing.WaterZ = WaterZ;

	const FCollisionQueryParams Params(SCENE_QUERY_STAT(LureCastLanding), false, IgnoreActor);

	// Something solid on the way (a wall, a rock): land just in front of it.
	if (World)
	{
		const float AimZ = Landing.bFoundWater ? WaterZ + 30.f : static_cast<float>(Origin.Z);
		FHitResult Hit;
		if (TraceCast(World, Hit, Origin, FVector(TargetXY.X, TargetXY.Y, AimZ), Params) && !Hit.bStartPenetrating)
		{
			const FVector2D HitXY(Hit.Location.X, Hit.Location.Y);
			const float Back = FMath::Min(20.f, static_cast<float>(FVector2D::Distance(StartXY, HitXY)));
			TargetXY = HitXY - Dir * Back;
			Landing.bBlocked = true;
			Landing.bFoundWater = FindWaterSurfaceZ(World, TargetXY, Settings, WaterZ);
			Landing.WaterZ = WaterZ;
		}
	}

	// Ground above the water surface = land (a dock, a beach, a rock); otherwise the bobber floats on the water.
	// T-071: search from at most LandingSearchHeight above the origin/water, and never from above a roof over the caster:
	// the bobber's flight starts under that roof, so it can't come down on top of it (it lands on the ground/water below).
	const float SearchHeight = FMath::Max(0.f, Profile ? Profile->LandingSearchHeight : FLureFishingRow().LandingSearchHeight);
	float TopZ = FMath::Max(static_cast<float>(Origin.Z), Landing.bFoundWater ? WaterZ : static_cast<float>(Origin.Z)) + SearchHeight;
	if (World)
	{
		FHitResult Ceiling;
		if (TraceCast(World, Ceiling, Origin, FVector(Origin.X, Origin.Y, TopZ), Params) && !Ceiling.bStartPenetrating)
		{
			TopZ = FMath::Min(TopZ, static_cast<float>(Ceiling.ImpactPoint.Z) - 1.f);
		}
	}
	const float BottomZ = Landing.bFoundWater ? WaterZ - 1.f : static_cast<float>(Origin.Z) - 100000.f;
	FHitResult Ground;
	const bool bGround = TraceCast(World, Ground, FVector(TargetXY.X, TargetXY.Y, TopZ), FVector(TargetXY.X, TargetXY.Y, BottomZ), Params);
	if (Landing.bFoundWater && (!bGround || Ground.ImpactPoint.Z <= WaterZ + Settings.LandTolerance))
	{
		Landing.Rest = FVector(TargetXY.X, TargetXY.Y, WaterZ);
		Landing.bOnWater = true;
	}
	else if (bGround)
	{
		Landing.Rest = Ground.ImpactPoint;
		Landing.bOnWater = false;
	}
	else
	{
		Landing.Rest = FVector(TargetXY.X, TargetXY.Y, Origin.Z); // nothing below at all: it just stays there, nothing bites
		Landing.bOnWater = false;
	}
	return Landing;
}

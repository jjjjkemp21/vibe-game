// Lure: dev-only console commands for scripted playtests (T-025). Compiled out of Shipping.

#include "Dev/LureDevCommands.h"

DEFINE_LOG_CATEGORY(LogLureDev);

#if !UE_BUILD_SHIPPING

#include "Character/LurePlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fish/FishInstance.h"
#include "Fish/FishRoll.h"
#include "Fish/FishSettings.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "UObject/Class.h"

const TCHAR* const FLureDevCommands::TeleportCommand = TEXT("Lure.Teleport");
const TCHAR* const FLureDevCommands::SetStanceCommand = TEXT("Lure.SetStance");
const TCHAR* const FLureDevCommands::GiveFishCommand = TEXT("Lure.GiveFish");

namespace LureDevCommandsPrivate
{
	/** Feedback goes to the log (Python reads it) and to the console that ran the command. */
	void Report(FOutputDevice& Ar, bool bOk, const FString& Text)
	{
		if (bOk)
		{
			UE_LOG(LogLureDev, Display, TEXT("%s"), *Text);
		}
		else
		{
			UE_LOG(LogLureDev, Warning, TEXT("%s"), *Text);
		}
		if (&Ar != static_cast<FOutputDevice*>(GLog))
		{
			Ar.Log(Text);
		}
	}

	/** A plain number ("-6000", "1.5"); FCString::IsNumeric alone also accepts a bare sign ("-"). */
	bool IsNumber(const FString& Text)
	{
		bool bHasDigit = false;
		for (const TCHAR Char : Text)
		{
			bHasDigit |= FChar::IsDigit(Char);
		}
		return bHasDigit && FCString::IsNumeric(*Text);
	}

	bool ParseInt(const FString& Text, int32& Out)
	{
		if (!IsNumber(Text) || Text.Contains(TEXT(".")))
		{
			return false;
		}
		Out = FCString::Atoi(*Text);
		return true;
	}

	FString PlayerLabel(const APlayerController* PC)
	{
		if (!PC)
		{
			return TEXT("(no player)");
		}
		const APlayerState* PS = PC->PlayerState;
		return PS ? FString::Printf(TEXT("%s (PlayerId %d)"), *PC->GetName(), PS->GetPlayerId()) : PC->GetName();
	}

	/** Parses "x,y,z" (a CastFrom tag value). */
	bool ParseVector(const FString& Text, FVector& Out)
	{
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() != 3)
		{
			return false;
		}
		for (const FString& Part : Parts)
		{
			if (!IsNumber(Part.TrimStartAndEnd()))
			{
				return false;
			}
		}
		Out = FVector(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
		return true;
	}

	/** The value of the first "Key=Value" tag with this key (case-insensitive), or false. */
	bool FindTagValue(const AActor& Actor, const TCHAR* Key, FString& OutValue)
	{
		const FString Prefix = FString(Key) + TEXT("=");
		for (const FName& Tag : Actor.Tags)
		{
			const FString TagText = Tag.ToString();
			if (TagText.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				OutValue = TagText.RightChop(Prefix.Len());
				return true;
			}
		}
		return false;
	}

	/** Match tier of Actor for Id (0 = best, INDEX_NONE = no match); OutHow describes the match. */
	int32 MatchTier(const AActor& Actor, const FString& Id, FString& OutHow)
	{
		const FString TeleportTag = TEXT("Teleport=") + Id;
		const FString TeleportTagTp = TEXT("Teleport=tp_") + Id;
		int32 Best = INDEX_NONE;
		auto Offer = [&Best, &OutHow](int32 Tier, const FString& How)
		{
			if (Best == INDEX_NONE || Tier < Best)
			{
				Best = Tier;
				OutHow = How;
			}
		};
		for (const FName& Tag : Actor.Tags)
		{
			const FString TagText = Tag.ToString();
			if (TagText.Equals(TeleportTag, ESearchCase::IgnoreCase) || TagText.Equals(TeleportTagTp, ESearchCase::IgnoreCase))
			{
				Offer(0, FString::Printf(TEXT("tag \"%s\""), *TagText));
			}
			else if (TagText.Equals(Id, ESearchCase::IgnoreCase))
			{
				Offer(1, FString::Printf(TEXT("tag \"%s\""), *TagText));
			}
			else
			{
				int32 EqualsAt = INDEX_NONE;
				if (TagText.FindChar(TEXT('='), EqualsAt) && TagText.RightChop(EqualsAt + 1).Equals(Id, ESearchCase::IgnoreCase))
				{
					Offer(2, FString::Printf(TEXT("tag \"%s\""), *TagText));
				}
			}
		}
		if (Best == INDEX_NONE && (Actor.GetName().Equals(Id, ESearchCase::IgnoreCase) || Actor.GetActorNameOrLabel().Equals(Id, ESearchCase::IgnoreCase)))
		{
			Offer(3, FString::Printf(TEXT("name \"%s\""), *Actor.GetActorNameOrLabel()));
		}
		return Best;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Worlds and players
// ---------------------------------------------------------------------------------------------------------------------

UWorld* FLureDevCommands::ResolveWorld(UWorld* InWorld)
{
	if (InWorld && InWorld->IsGameWorld())
	{
		return InWorld;
	}
	if (!GEngine)
	{
		return nullptr;
	}
	UWorld* ClientWorld = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (!World || !World->IsGameWorld())
		{
			continue;
		}
		if (World->GetNetMode() != NM_Client)
		{
			return World;
		}
		if (!ClientWorld)
		{
			ClientWorld = World;
		}
	}
	return ClientWorld;
}

bool FLureDevCommands::HasAuthority(const UWorld* World)
{
	return World && World->GetNetMode() != NM_Client;
}

APlayerController* FLureDevCommands::FindPlayer(UWorld* World, const TOptional<int32>& PlayerId, FString& OutError)
{
	if (!World)
	{
		OutError = TEXT("no game world (start PIE first)");
		return nullptr;
	}
	if (PlayerId.IsSet())
	{
		TArray<FString> Known;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (PC && PC->PlayerState)
			{
				if (PC->PlayerState->GetPlayerId() == PlayerId.GetValue())
				{
					return PC;
				}
				Known.Add(FString::FromInt(PC->PlayerState->GetPlayerId()));
			}
		}
		OutError = FString::Printf(TEXT("no player with PlayerId %d in this world (PlayerIds here: %s)"), PlayerId.GetValue(),
			Known.Num() ? *FString::Join(Known, TEXT(", ")) : TEXT("none"));
		return nullptr;
	}
	if (GEngine && GEngine->GetWorldContextFromWorld(World))
	{
		if (APlayerController* Local = GEngine->GetFirstLocalPlayerController(World))
		{
			return Local;
		}
	}
	APlayerController* First = nullptr;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		if (PC->IsLocalController())
		{
			return PC;
		}
		if (!First)
		{
			First = PC;
		}
	}
	if (!First)
	{
		OutError = TEXT("no player controller in this world");
	}
	return First;
}

bool FLureDevCommands::TakeKeyValue(TArray<FString>& Args, const TCHAR* Key, FString& OutValue)
{
	const FString Prefix = FString(Key) + TEXT("=");
	for (int32 Index = 0; Index < Args.Num(); ++Index)
	{
		if (Args[Index].StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			OutValue = Args[Index].RightChop(Prefix.Len());
			Args.RemoveAt(Index);
			return true;
		}
	}
	return false;
}

bool FLureDevCommands::TakePlayerId(TArray<FString>& Args, TOptional<int32>& OutPlayerId, FString& OutError)
{
	FString Value;
	if (!TakeKeyValue(Args, TEXT("Player"), Value))
	{
		return true;
	}
	int32 Id = 0;
	if (!LureDevCommandsPrivate::ParseInt(Value, Id))
	{
		OutError = FString::Printf(TEXT("Player=%s is not a PlayerId (an integer)"), *Value);
		return false;
	}
	OutPlayerId = Id;
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Teleport
// ---------------------------------------------------------------------------------------------------------------------

bool FLureDevCommands::FindTeleportTarget(UWorld* World, const FString& Id, int32 Index, FLureTeleportTarget& OutTarget, FString& OutError)
{
	using namespace LureDevCommandsPrivate;

	OutTarget = FLureTeleportTarget();
	if (!World)
	{
		OutError = TEXT("no game world (start PIE first)");
		return false;
	}
	const FString CleanId = Id.TrimStartAndEnd();
	if (CleanId.IsEmpty())
	{
		OutError = TEXT("no marker id given");
		return false;
	}

	int32 BestTier = INDEX_NONE;
	TArray<TPair<AActor*, FString>> Matches;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}
		FString How;
		const int32 Tier = MatchTier(*Actor, CleanId, How);
		if (Tier == INDEX_NONE || (BestTier != INDEX_NONE && Tier > BestTier))
		{
			continue;
		}
		if (BestTier == INDEX_NONE || Tier < BestTier)
		{
			BestTier = Tier;
			Matches.Reset();
		}
		Matches.Emplace(Actor, How);
	}
	if (Matches.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no marker \"%s\" (tried tags Teleport=%s / Teleport=tp_%s, a tag \"%s\", a tag \"<Key>=%s\" and actor names)"),
			*CleanId, *CleanId, *CleanId, *CleanId, *CleanId);
		return false;
	}
	Matches.Sort([](const TPair<AActor*, FString>& A, const TPair<AActor*, FString>& B)
	{
		const int32 ByLabel = A.Key->GetActorNameOrLabel().Compare(B.Key->GetActorNameOrLabel(), ESearchCase::IgnoreCase);
		return ByLabel != 0 ? ByLabel < 0 : A.Key->GetName() < B.Key->GetName();
	});
	if (Index < 0 || Index >= Matches.Num())
	{
		OutError = FString::Printf(TEXT("\"%s\" matched %d actor(s); index %d is out of range (0..%d)"), *CleanId, Matches.Num(), Index, Matches.Num() - 1);
		return false;
	}

	AActor* Marker = Matches[Index].Key;
	OutTarget.Marker = Marker;
	OutTarget.MatchedBy = Matches[Index].Value;
	OutTarget.NumMatches = Matches.Num();
	OutTarget.FeetLocation = Marker->GetActorLocation();
	OutTarget.ViewRotation = FRotator(0.f, Marker->GetActorRotation().Yaw, 0.f);

	FString CastFromText;
	FVector CastFrom;
	if (FindTagValue(*Marker, TEXT("CastFrom"), CastFromText) && ParseVector(CastFromText, CastFrom))
	{
		// Fishing spot: stand where you cast from, facing the spot.
		OutTarget.FeetLocation = CastFrom;
		const FVector ToSpot = Marker->GetActorLocation() - CastFrom;
		if (ToSpot.Size2D() > 1.f)
		{
			OutTarget.ViewRotation = FRotator(0.f, ToSpot.Rotation().Yaw, 0.f);
		}
		OutTarget.MatchedBy += TEXT(", standing at its CastFrom");
	}
	return true;
}

bool FLureDevCommands::ResolveTeleportArgs(UWorld* World, const TArray<FString>& Args, FLureTeleportTarget& OutTarget, FString& OutError)
{
	using namespace LureDevCommandsPrivate;

	if (Args.IsEmpty())
	{
		OutError = TEXT("usage: Lure.Teleport <marker> [Index] | <X> <Y> <Z> [Yaw]  [Player=<PlayerId>]");
		return false;
	}
	if (Args.Num() >= 3 && IsNumber(Args[0]) && IsNumber(Args[1]) && IsNumber(Args[2]))
	{
		if (Args.Num() > 4 || (Args.Num() == 4 && !IsNumber(Args[3])))
		{
			OutError = TEXT("usage: Lure.Teleport <X> <Y> <Z> [Yaw] (numbers, cm and degrees)");
			return false;
		}
		OutTarget = FLureTeleportTarget();
		OutTarget.FeetLocation = FVector(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
		if (Args.Num() == 4)
		{
			OutTarget.ViewRotation = FRotator(0.f, FCString::Atof(*Args[3]), 0.f);
		}
		OutTarget.MatchedBy = TEXT("coordinates");
		return true;
	}
	int32 Index = 0;
	if (Args.Num() > 2 || (Args.Num() == 2 && !ParseInt(Args[1], Index)))
	{
		OutError = TEXT("usage: Lure.Teleport <marker> [Index] (Index = which match, 0 = first by name)");
		return false;
	}
	return FindTeleportTarget(World, Args[0], Index, OutTarget, OutError);
}

FVector FLureDevCommands::ProbeFloor(const UWorld* World, const FVector& Feet, const AActor* Ignore)
{
	if (!World)
	{
		return Feet;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LureDevTeleportFloor), /*bTraceComplex*/ false, Ignore);
	FHitResult Hit;
	const FVector Start = Feet + FVector(0.f, 0.f, FloorProbeUp);
	const FVector End = Feet - FVector(0.f, 0.f, FloorProbeDown);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params) && !Hit.bStartPenetrating)
	{
		return FVector(Feet.X, Feet.Y, Hit.ImpactPoint.Z);
	}
	return Feet;
}

bool FLureDevCommands::TeleportPawn(APawn* Pawn, const FVector& FeetLocation, const TOptional<FRotator>& ViewRotation, FString& OutError, FVector* OutFeet)
{
	if (!Pawn)
	{
		OutError = TEXT("the player has no pawn");
		return false;
	}
	if (!Pawn->HasAuthority())
	{
		OutError = TEXT("the pawn is not the server's copy (teleports run on the server)");
		return false;
	}
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	const float HalfHeight = Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: Pawn->GetSimpleCollisionHalfHeight();

	const FVector Feet = ProbeFloor(Pawn->GetWorld(), FeetLocation, Pawn);
	const FVector Center = Feet + FVector(0.f, 0.f, HalfHeight + FloorClearance);
	const FRotator ActorRotation = ViewRotation.IsSet() ? FRotator(0.f, ViewRotation->Yaw, 0.f) : Pawn->GetActorRotation();
	if (!Pawn->TeleportTo(Center, ActorRotation, /*bIsATest*/ false, /*bNoCheck*/ false))
	{
		OutError = FString::Printf(TEXT("no room for the pawn at %s (blocked)"), *Feet.ToCompactString());
		return false;
	}
	if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
	{
		Movement->StopMovementImmediately();
	}
	if (ViewRotation.IsSet())
	{
		if (AController* Controller = Pawn->GetController())
		{
			// Runs locally for the host / standalone player; an RPC to the owning client otherwise.
			Controller->ClientSetRotation(ViewRotation.GetValue(), /*bResetCamera*/ true);
		}
	}
	if (OutFeet)
	{
		*OutFeet = Pawn->GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Stance
// ---------------------------------------------------------------------------------------------------------------------

bool FLureDevCommands::ParseStance(const FString& Text, ELureStance& OutStance)
{
	const UEnum* Enum = StaticEnum<ELureStance>();
	const FString Clean = Text.TrimStartAndEnd();
	for (int32 Index = 0; Enum && Index < Enum->NumEnums(); ++Index)
	{
		if (Enum->HasMetaData(TEXT("Hidden"), Index) || Enum->GetNameStringByIndex(Index).EndsWith(TEXT("_MAX")))
		{
			continue;
		}
		if (Enum->GetNameStringByIndex(Index).Equals(Clean, ESearchCase::IgnoreCase))
		{
			OutStance = static_cast<ELureStance>(Enum->GetValueByIndex(Index));
			return true;
		}
	}
	return false;
}

FString FLureDevCommands::StanceNames()
{
	TArray<FString> Names;
	const UEnum* Enum = StaticEnum<ELureStance>();
	for (int32 Index = 0; Enum && Index < Enum->NumEnums(); ++Index)
	{
		const FString Name = Enum->GetNameStringByIndex(Index);
		if (!Enum->HasMetaData(TEXT("Hidden"), Index) && !Name.EndsWith(TEXT("_MAX")))
		{
			Names.Add(Name);
		}
	}
	return FString::Join(Names, TEXT(", "));
}

bool FLureDevCommands::SetStance(ALurePlayerCharacter* Character, ELureStance Stance, FString& OutError)
{
	if (!Character)
	{
		OutError = TEXT("the player's pawn is not a Lure player character");
		return false;
	}
	if (!Character->IsLocallyControlled())
	{
		OutError = TEXT("SetStance acts like the stance key: run it in that player's own world (the Python driver: pd.set_stance(s, player=N))");
		return false;
	}
	Character->RequestStance(Stance);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Fish
// ---------------------------------------------------------------------------------------------------------------------

bool FLureDevCommands::ParseGiveFishArgs(const TArray<FString>& InArgs, FLureGiveFishArgs& OutArgs, FString& OutError)
{
	using namespace LureDevCommandsPrivate;

	OutArgs = FLureGiveFishArgs();
	TArray<FString> Args = InArgs;
	if (!TakePlayerId(Args, OutArgs.PlayerId, OutError))
	{
		return false;
	}
	FString Value;
	if (TakeKeyValue(Args, TEXT("Mods"), Value))
	{
		OutArgs.bForceModifiers = true;
		TArray<FString> Ids;
		Value.ParseIntoArray(Ids, TEXT(","), true);
		for (const FString& ModId : Ids)
		{
			const FString Clean = ModId.TrimStartAndEnd();
			if (!Clean.IsEmpty() && Clean != TEXT("-"))
			{
				OutArgs.ModifierIds.Add(FName(*Clean));
			}
		}
	}
	if (TakeKeyValue(Args, TEXT("Weight"), Value))
	{
		const float Fraction = FCString::Atof(*Value);
		if (!IsNumber(Value) || Fraction < 0.f || Fraction > 1.f)
		{
			OutError = FString::Printf(TEXT("Weight=%s must be a number from 0 to 1"), *Value);
			return false;
		}
		OutArgs.WeightFraction = Fraction;
	}
	if (Args.IsEmpty() || Args[0].Contains(TEXT("=")))
	{
		OutError = TEXT("usage: Lure.GiveFish <SpeciesId> [Rarity|-] [Seed] [Mods=A,B] [Weight=0..1] [Player=<PlayerId>]");
		return false;
	}
	OutArgs.SpeciesId = FName(*Args[0]);
	for (int32 Index = 1; Index < Args.Num(); ++Index)
	{
		const FString& Token = Args[Index];
		int32 Seed = 0;
		if (ParseInt(Token, Seed))
		{
			if (OutArgs.Seed.IsSet())
			{
				OutError = FString::Printf(TEXT("two seeds given (%d and %s)"), OutArgs.Seed.GetValue(), *Token);
				return false;
			}
			OutArgs.Seed = Seed;
		}
		else if (Token.Contains(TEXT("=")))
		{
			OutError = FString::Printf(TEXT("unknown option \"%s\" (options: Mods=, Weight=, Player=)"), *Token);
			return false;
		}
		else
		{
			if (!OutArgs.RarityId.IsNone())
			{
				OutError = FString::Printf(TEXT("two rarities given (%s and %s)"), *OutArgs.RarityId.ToString(), *Token);
				return false;
			}
			const bool bRoll = Token == TEXT("-") || Token.Equals(TEXT("any"), ESearchCase::IgnoreCase) || Token.Equals(TEXT("random"), ESearchCase::IgnoreCase);
			if (!bRoll)
			{
				OutArgs.RarityId = FName(*Token);
			}
		}
	}
	return true;
}

bool FLureDevCommands::GiveFish(const FFishTables& Tables, const FLureGiveFishArgs& Args, FFishInstance& OutFish, FString& OutError)
{
	OutFish = FFishInstance();
	auto RowNames = [](const UDataTable* Table)
	{
		TArray<FString> Names;
		if (Table)
		{
			for (const FName& Name : Table->GetRowNames())
			{
				Names.Add(Name.ToString());
			}
		}
		return Names.Num() ? FString::Join(Names, TEXT(", ")) : FString(TEXT("none"));
	};
	if (!Tables.Species || !Tables.Rarities || !Tables.Stats)
	{
		OutError = TEXT("the fish tables are missing (UFishSettings: Species, Rarity and Stat tables)");
		return false;
	}
	if (!Tables.FindSpecies(Args.SpeciesId))
	{
		OutError = FString::Printf(TEXT("unknown species \"%s\" (species: %s)"), *Args.SpeciesId.ToString(), *RowNames(Tables.Species));
		return false;
	}
	if (!Args.RarityId.IsNone() && !Tables.FindRarity(Args.RarityId))
	{
		OutError = FString::Printf(TEXT("unknown rarity \"%s\" (rarities: %s)"), *Args.RarityId.ToString(), *RowNames(Tables.Rarities));
		return false;
	}
	for (const FName& ModifierId : Args.ModifierIds)
	{
		if (!Tables.FindModifier(ModifierId))
		{
			OutError = FString::Printf(TEXT("unknown modifier \"%s\" (modifiers: %s)"), *ModifierId.ToString(), *RowNames(Tables.Modifiers));
			return false;
		}
	}

	FFishRollContext Context;
	Context.SpeciesId = Args.SpeciesId;
	Context.Seed = Args.Seed.IsSet() ? Args.Seed.GetValue() : FFishRoll::MakeRandomSeed();
	Context.ForcedRarityId = Args.RarityId;
	Context.bForceModifiers = Args.bForceModifiers;
	Context.ForcedModifierIds = Args.ModifierIds;
	if (Args.WeightFraction.IsSet())
	{
		Context.bForceWeightFraction = true;
		Context.ForcedWeightFraction = Args.WeightFraction.GetValue();
	}
	if (!FFishRoll::Roll(Tables, Context, OutFish))
	{
		OutError = FString::Printf(TEXT("the roll failed for %s (seed %d); see the LogLureFish warning above"), *Args.SpeciesId.ToString(), Context.Seed);
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Console entry points
// ---------------------------------------------------------------------------------------------------------------------

bool FLureDevCommands::RunTeleport(const TArray<FString>& InArgs, UWorld* InWorld, FOutputDevice& Ar)
{
	using namespace LureDevCommandsPrivate;

	TArray<FString> Args = InArgs;
	TOptional<int32> PlayerId;
	FString Error;
	UWorld* World = ResolveWorld(InWorld);
	if (!TakePlayerId(Args, PlayerId, Error))
	{
		Report(Ar, false, FString::Printf(TEXT("%s: %s"), TeleportCommand, *Error));
		return false;
	}
	APlayerController* PC = FindPlayer(World, PlayerId, Error);
	if (!PC)
	{
		Report(Ar, false, FString::Printf(TEXT("%s: %s"), TeleportCommand, *Error));
		return false;
	}
	if (!HasAuthority(World))
	{
		const int32 MyId = PC->PlayerState ? PC->PlayerState->GetPlayerId() : INDEX_NONE;
		Report(Ar, false, FString::Printf(TEXT("%s runs on the server: use the host's console with Player=%d, or the Python driver (pd.teleport(..., player=N))"), TeleportCommand, MyId));
		return false;
	}
	FLureTeleportTarget Target;
	if (!ResolveTeleportArgs(World, Args, Target, Error))
	{
		Report(Ar, false, FString::Printf(TEXT("%s: %s"), TeleportCommand, *Error));
		return false;
	}
	FVector Feet;
	if (!TeleportPawn(PC->GetPawn(), Target.FeetLocation, Target.ViewRotation, Error, &Feet))
	{
		Report(Ar, false, FString::Printf(TEXT("%s: %s"), TeleportCommand, *Error));
		return false;
	}
	const AActor* Marker = Target.Marker.Get();
	Report(Ar, true, FString::Printf(TEXT("%s: %s -> %s (%s%s), feet at %s, yaw %s"),
		TeleportCommand, *PlayerLabel(PC),
		Marker ? *Marker->GetActorNameOrLabel() : TEXT("point"), *Target.MatchedBy,
		Target.NumMatches > 1 ? *FString::Printf(TEXT(", 1 of %d matches"), Target.NumMatches) : TEXT(""),
		*Feet.ToCompactString(),
		Target.ViewRotation.IsSet() ? *FString::SanitizeFloat(Target.ViewRotation->Yaw) : TEXT("unchanged")));
	return true;
}

bool FLureDevCommands::RunSetStance(const TArray<FString>& InArgs, UWorld* InWorld, FOutputDevice& Ar)
{
	using namespace LureDevCommandsPrivate;

	TArray<FString> Args = InArgs;
	TOptional<int32> PlayerId;
	FString Error;
	if (!TakePlayerId(Args, PlayerId, Error))
	{
		Report(Ar, false, FString::Printf(TEXT("%s: %s"), SetStanceCommand, *Error));
		return false;
	}
	ELureStance Stance = ELureStance::Stand;
	if (Args.Num() != 1 || !ParseStance(Args[0], Stance))
	{
		Report(Ar, false, FString::Printf(TEXT("%s: usage: %s <%s> [Player=<PlayerId>]"), SetStanceCommand, SetStanceCommand, *StanceNames()));
		return false;
	}
	UWorld* World = ResolveWorld(InWorld);
	APlayerController* PC = FindPlayer(World, PlayerId, Error);
	ALurePlayerCharacter* Character = PC ? Cast<ALurePlayerCharacter>(PC->GetPawn()) : nullptr;
	if (!PC || !SetStance(Character, Stance, Error))
	{
		Report(Ar, false, FString::Printf(TEXT("%s: %s"), SetStanceCommand, *Error));
		return false;
	}
	Report(Ar, true, FString::Printf(TEXT("%s: %s requested %s (stance now %s; it changes over the next frames, a blocked stand-up waits for room)"),
		SetStanceCommand, *PlayerLabel(PC), *StaticEnum<ELureStance>()->GetNameStringByValue(static_cast<int64>(Stance)),
		*StaticEnum<ELureStance>()->GetNameStringByValue(static_cast<int64>(Character->GetStance()))));
	return true;
}

bool FLureDevCommands::RunGiveFish(const TArray<FString>& InArgs, UWorld* InWorld, FOutputDevice& Ar, const FFishTables* TablesOverride, FFishInstance* OutFish)
{
	using namespace LureDevCommandsPrivate;

	FLureGiveFishArgs Args;
	FString Error;
	if (!ParseGiveFishArgs(InArgs, Args, Error))
	{
		Report(Ar, false, FString::Printf(TEXT("%s: %s"), GiveFishCommand, *Error));
		return false;
	}
	UWorld* World = ResolveWorld(InWorld);
	APlayerController* PC = nullptr;
	if (World)
	{
		if (!HasAuthority(World))
		{
			Report(Ar, false, FString::Printf(TEXT("%s runs on the server (fish rolls are server-only): use the host's console with Player=<PlayerId>"), GiveFishCommand));
			return false;
		}
		PC = FindPlayer(World, Args.PlayerId, Error);
		if (!PC && Args.PlayerId.IsSet())
		{
			Report(Ar, false, FString::Printf(TEXT("%s: %s"), GiveFishCommand, *Error));
			return false;
		}
	}

	FFishTables Tables;
	if (TablesOverride)
	{
		Tables = *TablesOverride;
	}
	else if (!GetDefault<UFishSettings>()->LoadTables(Tables, Error))
	{
		Report(Ar, false, FString::Printf(TEXT("%s: the fish tables did not load: %s"), GiveFishCommand, *Error));
		return false;
	}

	FFishInstance Fish;
	if (!GiveFish(Tables, Args, Fish, Error))
	{
		Report(Ar, false, FString::Printf(TEXT("%s: %s"), GiveFishCommand, *Error));
		return false;
	}
	// TODO(T-010): put the fish in the player's cooler once it exists; for now the roll is only logged.
	Report(Ar, true, FString::Printf(TEXT("%s: %s rolled %s (not stored: the cooler comes with T-010)"), GiveFishCommand, *PlayerLabel(PC), *Fish.ToString()));
	if (OutFish)
	{
		*OutFish = Fish;
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------------------------------------------------

namespace LureDevCommandsPrivate
{
	FAutoConsoleCommandWithWorldArgsAndOutputDevice TeleportConsoleCommand(
		FLureDevCommands::TeleportCommand,
		TEXT("Dev: teleport a player. Lure.Teleport <marker> [Index] | <X> <Y> <Z> [Yaw]  [Player=<PlayerId>]. Markers: tag Teleport=<id> (or tp_<id>), ")
		TEXT("a tag equal to <id>, a tag <Key>=<id>, or an actor name. Fishing spots put you at their CastFrom. Server/standalone only."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			FLureDevCommands::RunTeleport(Args, World, Ar);
		}),
		ECVF_Cheat);

	FAutoConsoleCommandWithWorldArgsAndOutputDevice SetStanceConsoleCommand(
		FLureDevCommands::SetStanceCommand,
		TEXT("Dev: ask the local player for a stance, like the stance keys. Lure.SetStance <Stand|Crouch|Prone> [Player=<PlayerId>]."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			FLureDevCommands::RunSetStance(Args, World, Ar);
		}),
		ECVF_Cheat);

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GiveFishConsoleCommand(
		FLureDevCommands::GiveFishCommand,
		TEXT("Dev: roll a fish with the real roll pipeline and log it (the cooler comes later). ")
		TEXT("Lure.GiveFish <SpeciesId> [Rarity|-] [Seed] [Mods=A,B] [Weight=0..1] [Player=<PlayerId>]. Server/standalone only."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			FLureDevCommands::RunGiveFish(Args, World, Ar);
		}),
		ECVF_Cheat);

	// TODO(T-006): Lure.Fishing.ForceBite [Species] [Player=<PlayerId>] once ULureFishingComponent is in main: make the
	// bobber of the target player's line get a bite now (server), using the spot's normal species pick unless one is given.
}

#endif // !UE_BUILD_SHIPPING

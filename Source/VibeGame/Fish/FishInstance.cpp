// Copyright Epic Games, Inc. All Rights Reserved.

#include "Fish/FishInstance.h"

float FFishInstance::GetStat(const FGameplayTag& StatTag, float Default) const
{
	if (StatTag.IsValid())
	{
		for (const FFishStatValue& Stat : Stats)
		{
			if (Stat.Tag == StatTag)
			{
				return Stat.Value;
			}
		}
	}
	return Default;
}

bool FFishInstance::HasStat(const FGameplayTag& StatTag) const
{
	return StatTag.IsValid() && Stats.ContainsByPredicate([&StatTag](const FFishStatValue& Stat) { return Stat.Tag == StatTag; });
}

FString FFishInstance::ToString() const
{
	FString StatText;
	for (const FFishStatValue& Stat : Stats)
	{
		FString Name = Stat.Tag.ToString();
		Name.RemoveFromStart(TEXT("Fish.Stat."));
		StatText += FString::Printf(TEXT("%s%s %.3f"), StatText.IsEmpty() ? TEXT("") : TEXT(", "), *Name, Stat.Value);
	}
	const FString Modifiers = ModifierIds.Num() > 0
		? FString::JoinBy(ModifierIds, TEXT("+"), [](const FName& Id) { return Id.ToString(); })
		: FString(TEXT("no modifiers"));
	return FString::Printf(TEXT("%s %s (%s) %.3f kg, level %d, %d coins, %d XP, difficulty %.3f, seed %d [%s]"),
		*RarityId.ToString(), *SpeciesId.ToString(), *Modifiers, WeightKg, Level, Value, Xp, DifficultyRating, Seed, *StatText);
}

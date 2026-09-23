// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FFishTables;

/**
 *  Production validator for the four fish tables (tests, and later the editor on reimport).
 *  Returns one readable problem per issue (naming the table, row and field); empty = valid.
 *
 *  Checks: tables present with the right row structs; at least one species, rarity and stat row.
 *  Stats: tag registered and under Fish.Stat, unique; Min <= Default <= Max; a Fish.Stat.Weight row with Min > 0 that is
 *    not a difficulty stat.
 *  Species: DisplayName and JournalText set; BaseLevel >= 1; 0 < WeightMin <= WeightMax; the range inside the Weight
 *    stat's [Min, Max]; ReferenceWeight > 0; SizeSkew > 0; finite WeightStatExponent; BaseStats registered in
 *    DT_FishStat, not Weight, no duplicates, within the stat range; BaseValuePerKg >= 0; BiteWeight >= 0; time windows
 *    in [0, 24] with Start != End; tags registered and in their category (Habitat, Region, Weather, Bait/Hook,
 *    Fish.Family/Fish.Trait); allowed rarity/modifier ids exist, no duplicates; MaxModifiers >= 0; FightPatternId set;
 *    at least one allowed rarity with RollWeight > 0 (else it can roll nothing).
 *  Rarities: DisplayName; Rank >= 0 and unique; RollWeight finite and >= 0; Value/Xp multipliers > 0; StatMods valid;
 *    ValueMultiplier strictly increases with Rank; among enabled tiers (RollWeight > 0) RollWeight does not increase
 *    with Rank (ties allowed; disabled 0-weight tiers are staged content and skipped by this rule); among enabled tiers
 *    XpMultiplier does not decrease with Rank (ties allowed).
 *  Modifiers: DisplayName; RollChance in [0, 1]; ValueMultiplier > 0; StatMods valid (stat in DT_FishStat, finite,
 *    Multiply > 0); SpeciesIds exist; condition tags registered and in category; time windows valid.
 *  Tags under Test.* (automation fixtures) skip the category check, never the registration check.
 */
struct FFishDataValidator
{
	static TArray<FString> Validate(const FFishTables& Tables);

	/**
	 *  Raw check of a JSON table source (data/tables/DT_*.json), run next to the import and Validate. Every int or float
	 *  field (enums excepted) must be a JSON number and every bool JSON true/false, down into struct objects and arrays.
	 *  Structs other than gameplay tags must be JSON objects. Why: the engine's JSON import turns text in an int field
	 *  into 0 ("Rank": "high" -> 0) and text in a bool field into false without reporting a problem (text in a float
	 *  field is already an import problem). TableName names the table in problems (e.g. "DT_FishRarity").
	 */
	static TArray<FString> ValidateJsonSource(const FString& Json, const UScriptStruct* RowStruct, const FString& TableName);
};

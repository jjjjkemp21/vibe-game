# Fish system rules (T-008+), lead decisions 2026-09-22

Answers to the qa-engineer's T-008 test-design questions (Saved/AgentLogs/qa/eng2-T008-test-design.md, section 6). Binding for the implementation and the tests. Principle: a new species, rarity tier, modifier or stat is a data edit.

## API shape
- **Dependency-injected, world-free core (S1):** the roll and the bite picker take an `FFishTables` bundle (const UDataTable* for species, rarity, modifier, stat) and a context as parameters, with no reads from settings, subsystems, GWorld or the global RNG inside the core. Convenience wrappers that fetch the tables from settings sit on top.
- **Pure helpers take the uniform draw explicitly (S4):** e.g. `PickWeightedIndex(Weights, U)`, `SampleWeight(Species, U)`. Guard the fall-through: never return a 0-weight entry.
- **Forced overrides in the context (S5):** optional forced rarity, forced modifier list and forced weight fraction (used by tests and by T-025 `Lure.GiveFish`).
- **Failure contract (S6, Q22):** the functions return bool (plus an out instance or id). Log category `LogLureFish`. Data problems log a Warning; never check/ensure on data. Never produce NaN.
- **Production validator (S3, Q29):** `FFishDataValidator::Validate(Tables) -> TArray<FString>` (problems). Reused on editor reimport later.
- **Thread-safety (Q24):** the core is pure and read-only on the tables, so it may run off the game thread (no UObject creation inside).
- **Server authority (Q30):** gameplay calls the roll on the server only (wired in T-006). By convention the core has no net code.

## Data model
- **Formats (Q31):** JSON sources `data/tables/DT_FishSpecies.json`, `DT_FishRarity.json`, `DT_FishModifier.json`, `DT_FishStat.json`.
- **Stats are data (Q16, Q23):** `DT_FishStat` rows = stat registry: Tag (`Fish.Stat.*`), DisplayName, Min, Max, Default, bIsDifficultyStat. Tags live in `Config/Tags/*.ini` (text), not native C++ tags. Validation: every stat tag used anywhere exists in DT_FishStat. Global clamp = the row's [Min, Max]; a stat with no row is a validation error. Weight is a stat too (`Fish.Stat.Weight`).
- **Ids (Q15):** FName row names everywhere (instances, saves, replication). A saved id that no longer exists: keep the record, show "Unknown" in UI, and keep its stored value.
- **Rarity rows (Q10, Q6):** Rank (int, explicit order), RollWeight (0 = disabled), ValueMultiplier, XpMultiplier, LevelBonus (Q21), StatMods (same struct as modifiers; this is how rarity raises difficulty), CueColor. Validation: ValueMultiplier strictly increases with Rank; RollWeight does not increase with Rank (ties allowed).
- **Modifier rows:** StatMods [{StatTag, Op Add|Multiply, Value}], ValueMultiplier, RollChance (0..1), ExclusivityGroup (FName, None = none), conditions (species tags, region tags, time windows, weather tags; empty = any).
- **Species rows:** BaseLevel, WeightMin/WeightMax (kg), SizeSkew, ReferenceWeight, WeightStatExponent, BaseStats [{Tag, Value}], BaseValuePerKg, BiteWeight (abundance, Q19), Habitat/Region tags, TimeWindows, WeatherTags (Q32; empty = any), AcceptedBait tags, AllowedRarities / AllowedModifiers (empty = all, Q11; validation warns if a species can roll nothing), MaxModifiers (cap, Q3), FightPatternId, JournalText, soft mesh.

## Pipeline (one function), order and formulas (S7)
Each stage uses its own RNG sub-stream: `FRandomStream(HashCombine(Seed, StageId))`, with StageId constant per stage (Q13). So adding rows or changing luck doesn't reshuffle other stages. Every int32 seed is valid; 0 is not special (Q14). Seeds come from a server RNG at bite time (T-006).
1. **Species base:** BaseStats; missing stats take the DT_FishStat Default.
2. **Weight:** u from the weight stream; w = WeightMin + (WeightMax - WeightMin) * u^SizeSkew (SizeSkew > 1 means most fish are small and big ones rare; min == max means fixed). Difficulty stats scale by (w / ReferenceWeight)^WeightStatExponent (factor 1.0 at ReferenceWeight). The base roll stays in [min, max].
3. **Rarity:** weighted pick over the allowed tiers with RollWeight > 0. Luck (>= 0, NaN -> 0, clamped to a settings max) boosts tiers by Rank: w_i' = w_i * (1 + Luck * Rank_i * LuckRankFactor); luck 0 gives exactly the base weights; 0-weight tiers never roll (Q1, Q2). Apply the rarity StatMods, then the rarity's LevelBonus.
4. **Modifiers:** candidates = allowed plus condition-eligible, iterated in lexical row-name order (Q12). Each rolls independently with RollChance (luck does not affect modifiers, Q25). No duplicates (Q3). If several in one ExclusivityGroup roll, keep one by a weighted pick (weights = RollChance) on the modifier stream (Q4). If more than MaxModifiers remain, keep the first MaxModifiers in row-name order. Apply ALL Adds (from every kept modifier), then ALL Multiplies as a product (Q5). Modifiers may push Weight above WeightMax (record fish, Q7).
5. **Clamp** every stat to its DT_FishStat [Min, Max].
6. **Value:** round-half-up( BaseValuePerKg * FinalWeight * Rarity.ValueMultiplier * Product(Modifier.ValueMultiplier) ), integer currency, minimum 1 (Q9). XP is stored too: round(BaseXp(level) * Rarity.XpMultiplier) (Q27; the base XP curve is a setting until T-010). Also store DifficultyRating = the mean of the difficulty stats relative to the species base (UI hint).
- Value-only modifiers (e.g. Albino) are allowed; difficulty-only ones too (Q28).
- Level (Q20, Q21): FishLevel = BaseLevel + Rarity.LevelBonus. Level hook: `LevelDifficultyMultiplier(FishLevel, PlayerLevel) = clamp(1 + max(0, d) * OverLevelFactor - max(0, -d) * UnderLevelFactor, MinMult, MaxMult)` with d = FishLevel - PlayerLevel; the factors and clamps are in settings (move to a curve table in T-010 if needed). The player level does not filter bites.

## Bite picker
- Context: region tag, habitat tag, time of day (float hours, [0, 24), 24.0 -> 0), weather tag (optional), bait tag (optional), seed.
- **Eligibility:** region and habitat match, time inside any window, weather matches (empty = any), bait accepted (Q18).
- **Time windows (Q17):** half-open [Start, End) in hours; Start > End wraps midnight. Named time tags (Time.Dawn, ...) come later from T-013 data mapped to hours.
- **Bait (Q18):** any-of, hierarchical (the player's Bait.Worm.Night satisfies a species accepting Bait.Worm); an empty AcceptedBait means any bait or none.
- **Pick:** weighted by BiteWeight among the eligible species (Q19). No eligible species: returns false with no warning (normal gameplay).

## Other
- **Instance:** GetStat is an exact tag match (Q34). Replicated size target: <= 256 bytes per typical instance (Q26); the test reports the actual size.
- **Test-only tags (Q33):** approved: `Test.Fish.*` and `Fish.Stat.QA_TestOnly`, defined only under WITH_DEV_AUTOMATION_TESTS.

## Confirmed implementation choices (lead, after lane eng2 3cbd3b6)
- "RollWeight doesn't increase with Rank" counts only ENABLED tiers (RollWeight > 0); disabled tiers are ignored by this check.
- Empty tag lists mean "any" for species habitat and region too (not only weather and bait).
- The modifier stream draws one number per modifier row in lexical order, eligible or not, so conditions don't shift other rows. Adding a modifier row can shift modifier results; weight and rarity stay stable.
- Lexical order = `FName::LexicalLess` (natural numeric suffixes: Mod_2 < Mod_10).
- The bite picker walks species in table row order (deterministic for the same data; fast at 1,000 species).
- ForcedWeightFraction is linear in [Min, Max] (no skew).
- Data problems met during a roll (Multiply <= 0, non-finite values, a stat with no DT_FishStat row) are skipped with a Warning.
- The validator's category checks skip tags under `Test.*` (they must still be registered).
- API names: see the `FishRoll.h` header comment (FFishRoll::Roll/PickSpecies, FFishTables, FFishRollContext, FFishInstance, FFishDataValidator, UFishSettings, UFishLibrary; log LogLureFish).

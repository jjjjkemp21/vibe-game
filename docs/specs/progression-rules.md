# Progression rules (T-010): money, XP, levels and prices

Implementation decisions by the unreal-engineer, 2026-09-23 (lane eng5). Code: `Source/VibeGame/Progression/`, `Source/VibeGame/Interaction/`, `Source/VibeGame/Game/LurePlayerState.*`. Tests: `Project.Progression.*` (`Source/VibeGame/Tests/Progression/`).

**T-030 (2026-09-23, lane eng4) moved the cooler and selling out of this spec:** the cooler is a physical world actor, fish are items with freshness, and a sell counter replaces the sell point. Those rules are in `docs/specs/catch-handling-rules.md`. This spec keeps money, XP, levels, the price formula, the market table and the level gap. Sections below that changed say so.

## Where things live
- **ALurePlayerState** (set as `ALureGameMode::PlayerStateClass`) owns the replicated `ULureProgressionComponent` (T-030: the cooler component moved to the cooler actor, `ALureCoolerActor`).
- Why the player state and not the pawn: it outlives the pawn, so money, XP and level survive respawn and a change of pawn (the boat later). Seamless travel and reconnects carry them over (`CopyProperties` / `OverrideWith` through the save struct). Coolers are world items and are never copied that way (no duplicated fish).
- Other systems reach it from any actor of the player (pawn, controller, player state, or something they own) through `ULureProgressionLibrary`.

## Data (CSV sources in `data/tables/`, imported to `/Game/Data/` by the editor-operator)
- `DT_PlayerLevel.csv` (`FPlayerLevelRow`): `Level` (1..N, no gaps), `XpToNext` (XP from the start of the level to the next; > 0 below the cap, 0 on the last level; must not decrease). Placeholder curve: 10 levels, 40, 70, 110, 160, 220, 290, 370, 460, 560 XP.
- `DT_Cooler.csv` (`FCoolerRow`): the cooler types. T-030 columns and rows (Starter 4 slots, Large 8) are in catch-handling-rules.md.
- `DT_FishMarket.csv` (`FFishMarketRow`): a buyer (dock or NPC): `DisplayName`, `SellMultiplier` (0 < x <= 10). `Default` = 1.0 (sell counters without a MarketId), `PalmKeyDock` = 1.0.
- Settings: Project Settings > Game > Progression (`[/Script/VibeGame.LureProgressionSettings]` in `Config/DefaultGame.ini`): table paths, `DefaultCoolerId`, `DefaultMarketId`, `FallbackCoolerSlots` (C++ default only, used only when DT_Cooler is missing), `StartingMoney`, `bShowPlaceholderText`.
- Validation: `FLureProgressionData::ValidatePlayerLevelTable / ValidateCoolerTable / ValidateMarketTable` and `ValidateCsvSource` (text in a number cell, a fraction in a whole-number cell, unknown or missing columns, duplicate row names). Reusable on reimport.
- Missing tables never break the game: no DT_PlayerLevel = XP still counts and the level waits; no DT_Cooler (or no default row) = `FallbackCoolerSlots` (set only in C++, `LureProgressionSettings.h`); a cooler id DT_Cooler doesn't have (e.g. a save from before a row was renamed or removed) switches to the `DefaultCoolerId` row, fish kept; no DT_FishMarket or row = multiplier 1. Each logs a Warning (the market once per sell point).

## Cooler (storage component)
- `ULureCoolerComponent` now lives on `ALureCoolerActor` and holds `FLureCaughtFish` records (the `FFishInstance`, never re-rolled or re-priced, plus freshness), in the order they were added, no gaps (slot = index). Everything replicates to everyone (a cooler is a shared world object). Rules: catch-handling-rules.md "The cooler".
- `AddFish` returns false when full, for an invalid fish (no species), or on a client. `RemoveFish(Slot)`, `RemoveLastFish`, `Clear()` (returns the count), `TakeAll()`, `SetCoolerId(Row)` (upgrade).
- A smaller cooler (or a loaded save over capacity) keeps every fish; adds are refused until there is room. Nothing is ever deleted by a size change.

## Landing a fish (the T-007 entry point)
- The whole landing is `ULureCatchLibrary::HandleFishLanded(Pawn, Fish)` (server, T-030): the XP now, and the fish item on the angler's hook.
- `ULureProgressionLibrary::HandleFishLanded(Pawn, Fish)` = `ULureProgressionComponent::HandleFishLanded` is the XP part only.
- XP = `Fish.Xp` (from the roll pipeline; the per-fish XP formula stays in `UFishSettings.RollTuning`). XP is given on landing, not on selling.
- `OnFishLanded` fires on the server (T-011 journal can listen).

## Selling
- Price of one fish = `max(1, round-half-up(Fish.Value * SellMultiplier))`, round-half-up(x) = floor(x + 0.5). `Fish.Value` already includes species, weight, rarity and modifiers (roll pipeline step 6): no second pricing formula. A real fish always pays at least 1 coin.
- A total is the sum of the per-fish prices (each fish rounded on its own), so selling all at once pays the same as one by one.
- T-030: freshness scales the price: `max(1, round-half-up(Fish.Value x ValueShare x SellMultiplier))` (`FLureFreshness::GetSellPrice`; a fresh fish has share 1, the T-010 price).
- T-030: `ALureSellCounter` replaces `ALureSellPoint`: fish are put on the counter and sold together; the seller is paid through `ULureProgressionComponent::RecordSale(FishSold, MoneyEarned)` (money + the sale notice). Rules: catch-handling-rules.md "The sell counter".
- Server checks: authority, the pawn within radius + 150 cm (`ILureInteractable::ServerRangeSlack`, network lag), the player has progression.

## Money
- `AddMoney(Amount > 0)`, `SpendMoney(Amount > 0)` (false and nothing spent if short; T-012 shop, T-017 repairs). Money is an int32 that saturates at MAX_int32 (never wraps).

## XP and levels
- `TotalXp` counts every XP ever earned. Level = the highest level whose start XP <= TotalXp, capped at the last row. XP past the cap keeps counting, so a raised cap levels players up when they load.
- The level never goes down. A multi-level jump fires **one** `OnLevelUp(OldLevel, NewLevel)` (listeners that give per-level rewards loop over the range).
- Events: server fires `OnLevelUp`, `OnLevelChanged`, `OnXpChanged`, `OnMoneyChanged` directly. Clients get them from the OnReps; a client fires `OnLevelUp` only when the replicated level rises after BeginPlay (the first values on joining are not level-ups).

## Level gap: "fish above your level escape easily"
- Reused, not duplicated: `FFishRoll::LevelDifficultyMultiplier(FishLevel, PlayerLevel, UFishSettings.LevelScaling)`, d = FishLevel - PlayerLevel, `clamp(1 + max(0,d)*OverLevelFactor - max(0,-d)*UnderLevelFactor, Min, Max)`. The factors are now explicit data in `Config/DefaultGame.ini` `[/Script/VibeGame.FishSettings] LevelScaling` (0.35 / 0.1 / 0.5 / 5.0: 1 level above = 1.35x, 4 above = 2.4x).
- For the fight (T-007): `ULureProgressionLibrary::GetFishDifficultyMultiplier(Pawn, Fish.Level)` (player level from the progression; level 1 if none). T-007 decides how the multiplier maps to pull, tension or escape.

## Getting caught (T-010 part of T-017)
- T-030: `ULureCatchLibrary::HandlePlayerCaught(Pawn)` (server): the fish in the hand and on the hook are lost, a carried cooler is put down, coolers stay (catch-handling-rules.md). Money, XP and level are kept. T-017 adds the respawn and gear wear around it.

## Save-ready (T-019)
- `FLureProgressSaveData` v2 (every field `SaveGame`): Version, Money, TotalXp, Level. `GetSaveData()` / `ApplySaveData(Data)` (server). T-030 removed the cooler fields (v1 was never written to disk); coolers are saved as world items (`FLureCoolerSaveData`, bundled with this in `FLurePlayerSaveData`).
- On load: Level = max(saved Level, the level the XP gives), capped at the curve's max (kept as saved when there is no curve); negative money/XP load as 0; loading never fires `OnLevelUp`.

## Interact key
- New Enhanced Input action `Interact` (Boolean) in `ULureInputSubsystem`, keys `InteractKeys` in `ULureCharacterSettings` (DefaultGame.ini): E and Gamepad_FaceButton_Left.
- `ULureInteractionComponent` on `ALurePlayerCharacter` binds it, picks the registered `ILureInteractable` you look at (`ULureInteractionSubsystem`, no collision needed), gives the prompt, and sends `ServerInteract` (reliable). T-030 added verbs and the second key (AltInteract, F): catch-handling-rules.md "Focus and input". NPCs (T-012) can implement the same interface.

## Placeholder UI
- Plain white text, top-left, drawn by `ALureHUD::DrawHUD` (same path as the fishing text; the debug-canvas hook was dropped after the T-006/T-010 playtest, B2): `Money 120   Level 3 (XP 40/110)   Cooler 3/4`, what the hands hold, and the use-key prompt (e.g. `[E] Sell 5 fish (230 coins)   [F] Take a fish back`). Off with `bShowPlaceholderText=False`. T-011 replaces it (`ULureProgressionComponent::GetStatusText`, `ULureInteractionComponent::GetPromptText`).
- Notices (fishing-loop playtest 2026-09-23): `Level up! Level 2` and `Sold 2 fish for 48 coins`, drawn under the top-left block for `NoticeSeconds` (DefaultGame.ini, 4 s), on the owning player's machine only: a level-up where the player state's owner is a local player controller (host: server path; client: `OnRep_Level`), a sale through the `ClientFishSold` RPC (also fires `OnFishSold` there). The fishing/fight text block is lower-left, bottom-anchored, so the centre of the view stays clear.

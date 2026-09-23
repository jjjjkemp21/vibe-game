# Progression rules (T-010): cooler, selling, money, XP and levels

Implementation decisions by the unreal-engineer, 2026-09-23 (lane eng5). Code: `Source/VibeGame/Progression/`, `Source/VibeGame/Interaction/`, `Source/VibeGame/Game/LurePlayerState.*`. Tests: `Project.Progression.*` (`Source/VibeGame/Tests/Progression/`).

## Where things live
- **ALurePlayerState** (set as `ALureGameMode::PlayerStateClass`) owns two replicated components: `ULureCoolerComponent` and `ULureProgressionComponent`.
- Why the player state and not the pawn: it outlives the pawn, so money, XP, level and the cooler survive respawn and a change of pawn (the boat later). The "caught" rule empties the cooler explicitly (see below) instead of relying on the pawn being destroyed. Seamless travel and reconnects carry everything over (`CopyProperties` / `OverrideWith` through the save struct).
- Other systems reach it from any actor of the player (pawn, controller, player state, or something they own) through `ULureProgressionLibrary`.

## Data (CSV sources in `data/tables/`, imported to `/Game/Data/` by the editor-operator)
- `DT_PlayerLevel.csv` (`FPlayerLevelRow`): `Level` (1..N, no gaps), `XpToNext` (XP from the start of the level to the next; > 0 below the cap, 0 on the last level; must not decrease). Placeholder curve: 10 levels, 40, 70, 110, 160, 220, 290, 370, 460, 560 XP.
- `DT_Cooler.csv` (`FCoolerRow`): `DisplayName`, `Slots` (1..100). `Basic` = 8 slots (every new player), `Large` = 14 (upgrade row, not sold yet).
- `DT_FishMarket.csv` (`FFishMarketRow`): a buyer (dock or NPC): `DisplayName`, `SellMultiplier` (0 < x <= 10). `Default` = 1.0 (sell points without a MarketId), `PalmKeyDock` = 1.0.
- Settings: Project Settings > Game > Progression (`[/Script/VibeGame.LureProgressionSettings]` in `Config/DefaultGame.ini`): table paths, `DefaultCoolerId`, `DefaultMarketId`, `FallbackCoolerSlots` (C++ default only, used only when DT_Cooler is missing), `StartingMoney`, `bShowPlaceholderText`.
- Validation: `FLureProgressionData::ValidatePlayerLevelTable / ValidateCoolerTable / ValidateMarketTable` and `ValidateCsvSource` (text in a number cell, a fraction in a whole-number cell, unknown or missing columns, duplicate row names). Reusable on reimport.
- Missing tables never break the game: no DT_PlayerLevel = XP still counts and the level waits; no DT_Cooler (or no default row) = `FallbackCoolerSlots` (set only in C++, `LureProgressionSettings.h`); a cooler id DT_Cooler doesn't have (e.g. a save from before a row was renamed or removed) switches to the `DefaultCoolerId` row, fish kept; no DT_FishMarket or row = multiplier 1. Each logs a Warning (the market once per sell point).

## Cooler
- Slots hold `FFishInstance` records (never re-rolled or re-priced), in the order they were added, no gaps (slot = index).
- `AddFish` returns false when full, for an invalid fish (no species), or on a client. `RemoveFish(Slot)`, `Clear()` (returns the count), `TakeAll()`, `SetCoolerId(Row)` (upgrade).
- A smaller cooler (or a loaded save over capacity) keeps every fish; adds are refused until there is room. Nothing is ever deleted by a size change.
- Replication: the fish list to the owning player only (`COND_OwnerOnly`); `CoolerId` and `Capacity` to everyone. Capacity is resolved on the server from the table.

## Landing a fish (the T-007 entry point)
- `ULureProgressionLibrary::HandleFishLanded(Pawn, Fish)` (server) = `ULureProgressionComponent::HandleFishLanded`.
- XP = `Fish.Xp` (from the roll pipeline; the per-fish XP formula stays in `UFishSettings.RollTuning`). XP is given on landing, not on selling.
- The fish goes into the cooler if there is room. **A full cooler releases the fish, but the XP still counts** (you did catch it). The result says which (`bStoredInCooler`).
- `OnFishLanded` fires on the server (T-011 journal can listen).

## Selling
- Price of one fish = `max(1, round-half-up(Fish.Value * SellMultiplier))`, round-half-up(x) = floor(x + 0.5). `Fish.Value` already includes species, weight, rarity and modifiers (roll pipeline step 6): no second pricing formula. A real fish always pays at least 1 coin.
- A total is the sum of the per-fish prices (each fish rounded on its own), so selling all at once pays the same as one by one.
- `ALureSellPoint` (placeable): `MarketId` (DT_FishMarket row, None = default), `InteractionRadius` (cm, default 300). The Interact key sells the whole cooler; a later UI can sell one slot through the same server path (`ULureInteractionComponent::RequestInteract(SellPoint, SlotIndex)`).
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
- `ULureProgressionLibrary::HandlePlayerCaught(Pawn)` (server): the cooler is emptied (returns the number of fish lost). Money, XP, level and the cooler row (an upgrade) are kept. T-017 adds the respawn and gear wear around it.

## Save-ready (T-019)
- `FLureProgressSaveData` (every field `SaveGame`): Version, Money, TotalXp, Level, CoolerId, CoolerFish. `GetSaveData()` / `ApplySaveData(Data)` (server).
- On load: Level = max(saved Level, the level the XP gives), capped at the curve's max (kept as saved when there is no curve); negative money/XP load as 0; invalid fish records are dropped; no cooler row = the default; loading never fires `OnLevelUp`.

## Interact key
- New Enhanced Input action `Interact` (Boolean) in `ULureInputSubsystem`, keys `InteractKeys` in `ULureCharacterSettings` (DefaultGame.ini): E and Gamepad_FaceButton_Left.
- `ULureInteractionComponent` on `ALurePlayerCharacter` binds it, picks the nearest registered `ILureInteractable` in range (`ULureInteractionSubsystem`, no collision needed), gives the prompt, and sends `ServerInteract` (reliable). Sell points are the first interactable; NPCs (T-012) can implement the same interface.

## Placeholder UI
- Plain white text, top-left, on every HUD (`AHUD::OnHUDPostRender`, so it doesn't touch `ALureHUD`): `Money 120   Level 3 (XP 40/110)   Cooler 5/8`, and near a sell point `[E] Sell 5 fish (230 coins)`. Off with `bShowPlaceholderText=False`. T-011 replaces it (`ULureProgressionComponent::GetStatusText`, `ULureInteractionComponent::GetPromptText`).

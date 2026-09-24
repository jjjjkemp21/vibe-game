# Catch handling rules (T-030): the fish on the hook, the hand, the physical cooler, freshness and the sell counter

Unreal-engineer decisions, 2026-09-23 (lane eng4), from Jimmy's playtest redesign (GAME_DESIGN.md "Player verbs" Landing
and "Collect"). Binding for the code in `Source/VibeGame/Catch/` (+ the edits listed under "Where the code lives") and
the tests `Project.Catch.*`. Everything after "Landed" belongs here; T-027 (pre-bite) and T-028 (fight) own the rest.
This replaces the abstract cooler and the sell point of T-010 (progression-rules.md keeps money, XP and levels).

## The model in one paragraph
A catch is an `FLureCaughtFish` record: the `FFishInstance` from the roll pipeline plus its freshness. It lives in
exactly one place at a time: as a **fish item** in the world (`ALureFishItem`: hanging on the hook, in a hand, or lying
loose on the ground or the sell counter) or as a **record inside a cooler** (`ALureCoolerActor`, no actor per fish).
Moving it never copies it: the server takes the record out of one place and puts it into the next. The **hands**
(`ULureHandsComponent` on the player) hold at most one item; a fish takes one hand, a cooler both. Every change is made
by the server; clients only ask ("I pressed E on this cooler, expecting Put in").

## Items and the carry model (reusable for bait, gear and boat cargo later)
- `ALureCarryableItem` is the base of every physical item: a replicated `Hold` (`FLureItemHold`: the holder pawn and
  the mode None, Hand or Hook, one value so both change together), plus a replicated `Placement` (rest point, rotation,
  drop start) when it is free. The engine's movement and attachment replication are off for items: every machine places
  the item itself from `Hold` and `Placement`. The item is the single source of truth; the hands component only caches
  what its pawn holds (refreshed from the items' OnReps, set directly on the server). A new item type is a subclass that
  answers: hold kind (one or two hands), arms pose, carry speed multiplier, item name, and its interactions.
- Presentation is local to each machine: on the holder's own machine a held item is attached to the first-person arms
  (fish: a hand bone; cooler: the arms component) and drawn as a first-person primitive, so it never clips into walls;
  every other machine attaches it to the holder's body at a third-person offset. Offsets and sockets are settings
  (`ULureCatchSettings`), so the animation-artist's clips are matched without code.
- A free item stands at its `Placement` (actor root = the gameplay truth at once); the mesh flies there along a short
  arc (`DropArcTime`) on every machine, like the bobber.

## Landing (the hand-off from the fight)
- `ULureFishingComponent::LandFish` calls `ULureCatchLibrary::HandleFishLanded(Pawn, Fish)` (server), the one entry
  point (T-025 `Lure.GiveFish` uses it too). It gives the XP now (`ULureProgressionComponent::HandleFishLanded`, the
  T-010 rule: XP on landing, never on selling) and spawns the fish item on the angler's hook.
- **Hanging:** the fish hangs by its mouth (`Mouth` bone or socket, else the nose end of its bounds) from the hang pivot
  on a line of `HangLineLength` (DT_Catch) with a damped pendulum (`FLureHangPendulum`: position-based, fixed length,
  world gravity, `HangDamping`). It swings when the player turns or walks. Each machine simulates its own swing (it is
  cosmetic). The pivot is `ULureHandsComponent::GetHangPivot()` = the fishing component's `GetLineStart()` (the rod tip
  for the owner, an estimate for others). The position-based step itself damps a little (about 0.2/s at 60 Hz);
  `HangDamping` dominates. A start point far off the line's reach is moved onto it without a kick.
- **T-032 seam (the physics line):** `ALureFishItem::OnHookedChanged` (static, every machine) fires `(Fish, true)` when a
  fish starts hanging and `(Fish, false)` when it leaves the hook. The line attaches with
  `Line->AttachEndActor(Fish, Fish->GetHangLineLength(), Fish->GetMouthOffset())` and calls
  `Fish->SetExternalHangDriver(Line)`: from then on the pendulum and the short placeholder line are off on that machine
  and the item leaves its attachment alone (the driver moves the actor). The driver is cleared automatically when the
  fish leaves the hook. Nothing else needs to know.
- **T-029 seam (the landed fight fish as the look):** in the `OnFightFishLanded` listener,
  `KeepLandedFish(Fish)`, then `ULureCatchSubsystem::Get(World)->OfferLandedVisual(FightFish, Fish)` on every machine.
  The fish item with the same catch (seed + species + rarity) adopts it: at once if it exists, else when it arrives
  (`ClaimLandedVisual` in its look setup). The adopted actor is attached to the item's visual root, the item's own meshes
  hide, the swing starts where the landed fish's mouth was, and the adopted actor is destroyed with the item. An offer
  nobody claims is destroyed after `LandedVisualTimeout` (5 s). The visual must stop moving itself once offered.
- **The glue (`ULureCatchLinkSubsystem`, every drawing machine, cosmetic):** binds the two seams above. The landed fight
  fish is offered; it is kept (`KeepLandedFish`) when an item adopts it at once (the server hangs the item earlier in the
  same frame) or on a network client (the item may replicate later); else the offer is withdrawn and T-029 removes the fish.
  A hooked item hangs on its holder's line if this machine has one (`ULureFishingComponent::GetLine`), else it keeps its
  pendulum. **Same frame:** T-029 places the fight fish in TG_LastDemotable, after the line's update (TG_PostUpdateWork).
  When the item adopts the landed fish (`ALureFishItem::OnLandedVisualAdopted`, fired inside that later event or when the
  item replicates), the item is put where the landed fish is drawn and the line is laid again from the rod tip to its mouth
  (Hide + DetachEndActor + AttachEndActor), so the line ends at this frame's fish mouth, not last frame's bobber point. From
  then on the line moves the fish inside its own update, so the fish and the line end never differ within a frame.
- While a fish hangs, casting is refused (reason `Busy`, "the line is already out": it is, with a fish on it). The rod
  stays in hand. A second landing (debug) drops the older hanging fish below the hook.
- Only the angler can use their hanging fish: **E = Grab** (into the hand), **F = Let it go** (it drops; see Drop).

## The hand
- Grabbing (a hanging fish, a loose fish, or a fish taken out of a cooler) puts it in the hand: the rod is stowed
  (`IsRodInHand` false, casting refused with `NoRod`, a line that is out comes in) and the arms pose is **HoldFish**.
- One item at a time. With a fish in hand you can't pick up anything else (a second fish landed by debug tools waits on
  the hook: no Grab until the hand is free); with a cooler in both hands you can't use anything but the cooler itself.
  While swimming the hands take nothing (`CanHoldItems`): no grab, pick-up or take-out, and coolers offer nothing.
- **Drop (F)** with a fish in hand: it is tossed `DropForward` in front along the view at hand height (a wall stops it
  just in front), pulled back toward you if it would land inside a standing cooler (coolers ignore the cast channel),
  then falls straight down: onto the ground, or into the water by the bobber's water rules
  (`FLureFishingSpots::ResolveLanding` with no flight of its own; a cast-style flight aimed at the water would hit the
  dock under your feet). On land (dock, beach, rock, the sell counter) it lies there on its side, still spoiling, and
  anyone can pick it up. **On water it is released**: it swims off (the item is removed) and the owner sees "Released
  the Bonefish". There is no separate throw-back verb: walk to the water and drop it. **Let go (F on the hook)** drops it
  the same way, straight down from under the rod tip.

## The cooler (physical, replicated, `ALureCoolerActor`)
- A DT_Cooler row gives each cooler type: `Slots` (Starter = **4**), `BodyMesh` and `LidMesh` (soft references;
  SM_Cooler_Starter + SM_Cooler_Starter_Lid; lid on the body socket `LidHinge`, opening = relative pitch 0 to
  `LidOpenPitch` 100 deg), `OpenDecayRate` / `ClosedDecayRate` (freshness speed inside), `CarrySpeedMultiplier`.
  Missing meshes = a placeholder box of the same size (44 x 64 x 39 cm). A new cooler type is a row.
- Gameplay collision is a box sized to the mesh bounds (placeholder size without a mesh): it blocks players when the
  cooler stands (you can step onto it) and is off while it is carried. The meshes never collide and never stop casts.
- Contents: `FLureCaughtFish` records in `ULureCoolerComponent` (the T-010 storage component, moved from the player
  state to the cooler): in order, no gaps, capacity from the row, replicated to everyone. Take-out takes the **last**
  fish put in (the top of the pile). A smaller row never deletes fish (adds wait for room).
- Verbs (E = Interact, F = Alt Interact; "you" = the player looking at it, within `ReachDistance`):

| You hold | Cooler | E | F |
|---|---|---|---|
| nothing | closed | Open | Pick up |
| nothing | open, has fish | Take out the top fish | Close |
| nothing | open, empty | Close | Close |
| a fish | any, room | Put it in (a closed lid opens and shuts around it: the lid state is kept) | Drop the fish |
| a fish | full | nothing (info "Cooler full (4/4)") | Drop the fish |
| this cooler | - | Put down | Put down |

- **Carry:** both hands, arms pose **CarryCooler**, rod stowed (no fishing), move speed x `CarrySpeedMultiplier` of
  the row on land (Starter 0.8; sprint too). The lid closes when you pick it up. Put down = in front of you at
  `PutDownDistance`, **its front (+X, the latch) toward you** (the lid hinges at the back and opens away from you), on
  the floor found below (`PutDownMaxFall`), only on dry, walkable ground **at most a step above your feet** (the
  movement's `MaxStepHeight` + 5 cm: never on a counter, table or crate top; in the air, from the ground below you, so a
  jump doesn't raise the limit) and **never in a sell counter's area**,
  where the box fits (tries 100 % and 85 % of the distance, then just clear of your capsule); else "No room to put the
  cooler down here" and you keep carrying. An open cooler must be closed (F) before F picks it up.
- Automatic put-down (server): going **prone** puts it down in front of you (hide first, come back for it); falling into
  the **water** puts it down at your last dry ground spot. Both turn its front toward you
  (`ALureCoolerActor::GetYawFacing`). It is never lost. Lying prone you can't pick a cooler up (F shows "Stand up to
  carry the cooler"); E still opens it.
- **Contents display (cosmetic, every rendering machine):** while the lid is open and the cooler stands, the top fish
  of the pile show inside, curled and stacked: the DT_CoolerDisplay row named like the cooler type. `Slots` = the
  animation-artist's slot table (`art/export/Fish/SK_Fish.anim.md` "Cooler display"), relative to the body's
  `Contents` socket, bottom of the pile first (slot 0 = the lowest shown fish; taking one out re-seats the rest). A
  fish goes to its slot's X, Y and bed Z + `LieOffsetCm` x its shown scale, turned by the slot (pitch 0, yaw, roll
  +-90 = which side is down). Shown scale = (Weight / ReferenceWeight)^(1/3), at most `MaxFishScale` (1.0: four fish
  fit under the lid; the record keeps its weight). `FishPose` + `PoseTime` = the held pose (`A_Fish_Curled`). The
  layout is made for the cooler **on the floor with its front toward a standing player** (eye 1.65 m, 0.6 m away);
  seen from beside a counter the liner's front wall hides most fish, which is why a put-down faces you and refuses
  raised tops. No row = the built-in layout (= the shipped Starter row); a missing pose = straight fish. A fish put
  into a closed cooler makes the lid open and shut around it (a replicated pulse id).

## Freshness (`FLureFreshness`, DT_Freshness)
- Time zero is the landing (server time). Each record keeps `ExposedSeconds` (seconds spent spoiling, rate-weighted)
  as an anchor: `Exposure(Now) = ExposedSeconds + Rate x (Now - AnchorTime)`, with `AnchorTime` in server world time.
  The rate changes only on events (put in, take out, lid open/close, load), so nothing ticks and clients show a live
  value from the replicated anchor. Rates: out of a cooler (hook, hand, ground, counter) **1**; in a cooler the row's
  `OpenDecayRate` (1) or `ClosedDecayRate` (**0: holds**).
- DT_Freshness row (the row named like the species wins, else `Default`): `GraceSeconds` (no loss), `SpoilSeconds`
  (from the end of grace to the minimum), `CurveExponent` (1 = linear; > 1 slow then fast), `MinValueShare` (floor).
  `x = clamp((Exposure - Grace) / Spoil, 0, 1)`, `Freshness = 1 - x ^ CurveExponent` (shown as %),
  `ValueShare = MinValueShare + (1 - MinValueShare) x Freshness`.
- Current value = `max(1, round-half-up(Fish.Value x ValueShare))`; the sale price is the T-010 formula with the share:
  `max(1, round-half-up(Fish.Value x ValueShare x SellMultiplier))`. The rolled `Fish.Value` is never changed.
- Missing table or row: the built-in Default row (same numbers as the CSV) and one warning.

## The sell counter (`ALureSellCounter`, replaces `ALureSellPoint`)
- Placed on the shop counter (the level's `sell_point` marker class): origin = the centre of the counter top, +X toward
  the customers, `CounterHalfSize` = the top area (half depth, half length, height above the top). `MarketId` = the
  DT_FishMarket row (PalmKeyDock). `InteractionRadius` = the prompt range.
- A fish is **on the counter** when it lands inside that box (placed with E, or dropped with F). Fish on a counter are
  part of it: they can't be picked up one by one (F takes the last one back).
- Verbs: holding a fish, **E = Put it on the counter** (it goes to the next free spot along the counter,
  `FishSpacing` apart). Empty hands with fish on the counter: **E = "Sell N fish (X coins)"**, F = Take back the last
  fish. Nothing on it: info "Put fish on the counter to sell them".
- Selling (server): every fish on the counter at its current price (freshness x market), money to the **seller**, the
  items are removed, and the seller sees "Sold N fish for X coins" (T-010 notice path `ClientFishSold`). XP is not given
  again. Fish on the counter keep spoiling until sold. The seller must be within `InteractionRadius` + 150 cm.
- The counter replicates (dormant after its first send) and its four settings replicate once, so a counter spawned at
  runtime works like one placed in the level. Old maps: a CoreRedirect in `Config/DefaultEngine.ini` maps
  `LureSellPoint` to `LureSellCounter`.

## Focus and input
- Keys: **Interact** (E / gamepad X, existing) and the new **AltInteract** (F / gamepad Y). LMB stays the fishing
  button (never drops a fish: habit clicks must not throw a catch away).
- What a key does: the target you look at (registered interactables within their reach; the smallest angle between
  your view and the target's shape, at most `FocusAngleDeg`; ties go to the nearer) if it has a verb for that key; else
  the item in your hands; else your hanging fish. A target with no verb for you (e.g. a cooler someone carries) can't
  take the focus. The HUD prompt shows both keys: `[E] Put the Bonefish in the cooler (2/4)   [F] Drop the Bonefish`.
- Network: the client sends `ServerInteract(Target, Key, Verb)`. The server checks the target (interactable, in its reach
  + `ServerRangeSlack` 150 cm) and that ITS own verb for that key is the same (so a stale prompt never does something
  else, e.g. two players taking the last fish: the second is refused), then performs it. The view angle is not
  re-checked on the server.

## Network (what replicates, what each machine does)
- Server-authoritative: every change goes through `Authority*` functions that refuse on clients (with a Warning).
- Replicated: items' `Hold` and `Placement`, a fish's record and counter; a cooler's lid, lid pulse, owner, guid,
  starter flag and its storage (records, row, capacity, rate), always relevant; the counter (see above).
- Each machine reads its own data tables for looks and names (DT_Cooler row, DT_Freshness, DT_Catch, DT_CoolerDisplay,
  the fish tables): the carrier's machine predicts the carry speed from its own DT_Cooler row, so every machine must
  have the same tables (they are cooked content).
- Freshness shown on clients uses the game state's synchronized server clock with the replicated anchor.

## Owner rules (co-op, 2-4 players)
- Coolers and loose fish are shared: anyone can open, close, put in, take out, carry them, and pick up a loose fish.
- One carrier at a time: a carried cooler can't be used by others until it is put down.
- A hanging fish belongs to its angler (only they can grab or release it).
- Selling pays the player who presses Sell for everything on the counter (whoever caught it). XP stayed with the angler.
- Each cooler records the player it was made for (`OwningPlayerState`, and a `CoolerGuid` for saves): that player's
  save keeps it. Use is not restricted by it.

## Starting cooler (data)
- When a player's pawn first spawns at a player start (`ALureGameMode::RestartPlayerAtPlayerStart`, server) and they own
  no cooler, a cooler of ULureProgressionSettings `DefaultCoolerId` (Starter) is spawned for them: at an actor tagged
  `Lure.CoolerSpawn` if the level has one (each player's cooler `StarterCoolerSpacing` further along its +Y, turned like
  the marker: point the marker's +X where players will stand), else at `StarterCoolerOffset` in the player start's frame
  with its front toward the start (players sharing one start, e.g. a map with one PlayerStart: the same row,
  `StarterCoolerSpacing` apart along the start's +Y); dropped onto the floor below. One rule for both: a slot with a
  cooler already standing in it (within half a spacing) is skipped, so starter coolers never stand inside each other. Respawns don't make another.
  `bSpawnStarterCooler` = False turns it off (settings).

## Leaving, falling in, getting caught
- **Water:** falling in empties the hands: a held or hanging fish drops at your last dry ground spot (it lies there), a
  carried cooler is put down there.
- **Leaving the game / pawn removed:** the same (nothing is lost).
- **Caught (T-017 calls `ULureCatchLibrary::HandlePlayerCaught(Pawn)`, server):** the fish in your hand and on your hook
  are lost; a carried cooler is put down at your last dry ground spot; coolers stay where they are (their fish spoil only
  if the lid is open). Money, XP and level are kept (progression-rules.md).

## Save (T-019)
- `FLureProgressSaveData` v2 is money, XP and level only (the abstract cooler fields are gone; v1 was never written to
  disk: T-019 isn't built). The player state still copies it for seamless travel and reconnects.
- World items: `ULureCatchLibrary::GetCoolerSaveData(PlayerState)` / `ApplyCoolerSaveData(PlayerState, Coolers)`
  save each cooler the player owns: guid, row, transform, lid, and every fish with its exposure evaluated at save time
  (the anchor is rebuilt from the lid on load). Apply matches coolers by their stable `CoolerGuid`: a cooler already in
  the world (a mid-session reconnect or a re-apply) wins and is skipped, its saved fish are NOT restored (fish taken out
  since the save still exist in a hand, on a counter or the ground, so restoring would copy them; only its owner is
  updated); only missing coolers are spawned from the save, with their fish. Apply never creates a second copy of a
  fish. It also removes this player's empty automatic starter cooler if the save has its own. Fish in hands,
  on hooks or lying around are not saved (saving happens at docks; T-019 may add them). `FLurePlayerSaveData` bundles
  both for T-019. Reconnects and travel copy progression only, never world items (no duplicated fish).

## HUD (placeholder text, `ALureHUD` top-left block)
- Status line: `Money 120   Level 3 (XP 40/110)   Cooler 3/4` (your own cooler).
- `Holding: Bonefish (Rare), 2.04 kg, 45 coins, fresh 87%` (value and freshness now), the E/F prompt line(s), and the
  info line (e.g. `Cooler full (4/4)`). Notices: "Released the Bonefish", "Sold 3 fish for 123 coins".

## Data
- `data/tables/DT_Cooler.csv` (FCoolerRow): Starter (4 slots, the starter meshes, open 1.0, closed 0.0, carry 0.8),
  Large (8 slots, upgrade row for the T-012 shop, placeholder look = the starter meshes, carry 0.7).
- `data/tables/DT_Freshness.csv` (FLureFreshnessRow): Default = grace 120 s, spoil 600 s, exponent 1.0, min 0.3.
- `data/tables/DT_Catch.csv` (FLureCatchRow, row Default): HangLineLength 40 cm, HangDamping 1.2 /s, ReachDistance
  250 cm, FocusAngleDeg 20, DropForward 60 cm, DropArcTime 0.35 s, PutDownDistance 80 cm,
  PutDownMaxFall 300 cm, LidOpenPitch 100 deg, LidOpenTime 0.25 s.
- `data/tables/DT_CoolerDisplay.json` (FLureCoolerDisplayRow, rows Starter and Large): the 4-slot table of
  SK_Fish.anim.md (`anim_fish_cooler.py`, 2026-09-23), FishPose `/Game/Art/Fish/A_Fish_Curled`, PoseTime 0,
  MaxFishScale 1.0, LieOffsetCm 4.25 (Bonefish 4.24, CoralSnapper 4.26: one value until the species table has look
  columns). Large reuses the starter slots (its top 4 fish show) until it has its own model. A validator checks that
  every row names a DT_Cooler row and that LieOffsetCm is in [0, 50].
- Project Settings > Game > Catch Handling (`[/Script/VibeGame.LureCatchSettings]`): table paths, the fish mesh search
  (the species row's `Mesh`, else `/Game/Art/Fish/SK_<Species>`, else `SM_<Species>`), attach sockets and offsets, the
  starter cooler spawn. Keys: `AltInteractKeys` in Lure Character settings.
- All numbers are PLACEHOLDER until Jimmy plays it.

## Where the code lives
- New: `Source/VibeGame/Catch/` (LureCatchTypes, LureCatchSettings, LureCatchSubsystem (per-world data, item registry,
  the T-029 offers), LureCarryableItem, LureFishItem, LureCoolerActor, LureSellCounter, LureHandsComponent,
  LureCatchLibrary). Tests `Source/VibeGame/Tests/Catch/` (`CatchTestUtils` has the test dock world).
- Changed: `Progression/LureCoolerComponent` (records with freshness, on the cooler actor, replicated to all),
  `Progression/LureProgression{Component,Library,Types}` (XP-only landing, save v2, sale notice for the counter),
  `Game/LurePlayerState` (no cooler), `Game/LureGameMode` (starter cooler), `Game/LureHUD`, `Interaction/*` (verbs,
  focus, two keys), `Character/FPArmsPose.h` (HoldFish, CarryCooler), `Character/LurePlayerCharacter` (hands component,
  pose override), `Character/LureCharacterMovementComponent` (carry speed), `Character/LureInputSubsystem` +
  `LureCharacterSettings` (AltInteract), `Dev/LureDevCommands` (GiveFish hangs the fish).
- `Fishing/LureFishingComponent.cpp` (kept minimal): the landing hand-off call, and the rod-stow / fish-on-hook checks in
  `IsRodInHand` and `GetConditions`.
- Removed: `Progression/LureSellPoint` (replaced by the counter; the level marker class changes in L_PalmKey.json; a
  CoreRedirect keeps old maps loading).

## Patterns for the next item (bait box, gear crate, boat cargo)
1. Subclass `ALureCarryableItem`; answer `GetHoldKind`, `GetHoldPose`, `GetCarrySpeedMultiplier`, `GetItemName` and
   `GetInteraction` / `PerformInteraction` (plus the presentation hooks `GetFirstPersonAttachment`,
   `GetThirdPersonAttachment`, `GetRestVisualTransform` if the defaults don't fit). Add its verbs at the end of
   `ELureInteractVerb` with a prompt text.
2. Change holders only through `ULureHandsComponent::Authority*` on the server; never write `Holder` from a client.
3. Records that live in containers carry their own state (like freshness); containers change a record's rate on events,
   never per tick.
4. Every verb is validated on the server by recomputing `GetInteraction(Pawn, Key)` and comparing the verb, so offer a
   verb only when `PerformInteraction` can do it (check the same conditions in both).
5. Test it like `Project.Catch.*`: a game world with the test dock (`LCT::FWorld`), the keys pressed through
   `ULureInteractionComponent::PressKey`, and a real server + clients (`UE::Net::FTestWorlds`) for anything shared.

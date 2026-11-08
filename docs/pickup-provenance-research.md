# Pickup provenance and location research

Research date: 2026-11-05. Scope: identify a practical source-discovery framework, investigate player-left loot, and preserve physical pickup-location information. No DLL, installed configuration, runtime behavior, commit or release was changed for this research.

The user clarified that the randomizer reference is **thefifthmatt's Item and Enemy Randomizer**.

## Conclusion

Use runtime pickup-record evidence to classify the actual acquisition, and optionally enrich it with a static map/item-lot index. Keep the resolved awarded item separate from its source. Neither item ID, proximity, an event flag alone, nor a recent inventory decrease is a reliable substitute for provenance.

A promising native record is still present at the existing world-award hook. Static inspection found a candidate player-drop marker and candidate object-location fields. Their layout relationships have evidence in the locked executable; their semantics and multiplayer preservation still need controlled live validation. Do not deploy a player-drop exclusion on the candidate bit alone yet.

## Relevant public sources

### Randomizer: thefifthmatt/SoulsRandomizers

At inspected revision `0f4c316ac5728fed7db78e3a54bd307cfd43bd8e`, [EldenLocationDataScraper](https://github.com/thefifthmatt/SoulsRandomizers/blob/0f4c316ac5728fed7db78e3a54bd307cfd43bd8e/RandomizerCommon/Elden/EldenLocationDataScraper.cs) associates treasure events with map parts and item-lot rows. It also resolves enemy/NPC and gathering-asset lot references and examines scripts/talk for other rewards. Its entity records include map/part identity and placement positions.

[LocationData](https://github.com/thefifthmatt/SoulsRandomizers/blob/0f4c316ac5728fed7db78e3a54bd307cfd43bd8e/RandomizerCommon/Item/LocationData.cs) models lot/shop/external locations separately from awarded items, and permits multiple entities for a location. This is a useful design reference, not a runtime detector of items left by players.

[EldenCoordinator](https://github.com/thefifthmatt/SoulsRandomizers/blob/0f4c316ac5728fed7db78e3a54bd307cfd43bd8e/RandomizerCommon/Core/EldenCoordinator.cs) handles overworld tile scale, dungeon conversion offsets and landmark descriptions. Coordinates must retain map context; the same numeric XYZ in different maps is not the same place. Base-game and DLC worlds must remain distinguishable.

### Runtime layouts and examples

[fromsoftware-rs FieldInsHandle](https://github.com/vswarte/fromsoftware-rs/blob/57023153d786c4b13fe95d9170e6dcd9d35093ea/crates/eldenring/src/cs/field_ins.rs) combines an instance selector with a block ID. [PlayerIns/ChrIns](https://github.com/vswarte/fromsoftware-rs/blob/57023153d786c4b13fe95d9170e6dcd9d35093ea/crates/eldenring/src/cs/chr_ins.rs) distinguishes player block position/current block, saved-safe position, and character chunk coordinates. These are different coordinate observations, not interchangeable fields. Its current [MapItemMan](https://github.com/vswarte/fromsoftware-rs/blob/57023153d786c4b13fe95d9170e6dcd9d35093ea/crates/eldenring/src/cs/item.rs) declaration is still a stub, so it does not provide a ready-to-use drop-source API.

[EldenRingMap's item extractor](https://github.com/egormagurin/EldenRingMap/blob/48f42e570ada1dd28717d7ce56aaeb92ee14521b/tools/extract_items.py) independently demonstrates static MSB treasure/part/lot/flag joins. That extractor omits entries without pickup flags and collapses placements per lot, which is suitable for its map display but should not become our complete runtime source classifier. Repeating materials and multiple instances need separate handling.

[ER Fog Tracker](https://github.com/rbignon/er-fog-tracker/blob/a8a017076912c27cbb4beb9c23d683b7e005bd66/mod/src/eldenring/game_state.rs) reads live player position/map information for tracking. It is a practical example of obtaining collector location, not proof of the dropped object's origin. Its map utilities are another reference for formatting map IDs.

### Native dropped-item leads

[The Grand Archives ItemDrop helper](https://github.com/The-Grand-Archives/Elden-Ring-CT-TGA/blob/393c439e72ad48128efdc5e30fc3feb4bfbd210f/CheatTable/CheatEntries/Open%20-%20The%20Grand%20Archives%20-%20Elden%20Ring/MiscWIP/Dependencies/Global%20Functions/ItemDrop_code.cea) publishes a drop-function signature and arguments named isMultiDropShare/isScatter. The signature resolves to RVA 0x55bea0 in our installed executable. This was used as an investigation lead only; none of that helper was executed or incorporated into the DLL.

[DarkScript3's ER EMEDF](https://soulsmods.github.io/emedf/er-emedf.html) lists dropped-item conditions 3[14]/3[15], marked unused, and a used MoveBloodstainAndDroppedItems instruction. These suggest additional native routines to investigate. An unused documented script condition is not evidence of a callable, verified native DLL API.

## Findings in our exact installed executable

All addresses below refer only to the executable already guarded by this project's full-build checks. They must not be reused blindly for another game build.

### Drop construction

1. Menu/inventory remove-and-drop candidate at RVA 0x785c90 obtains an item descriptor, removes a positive quantity via a helper, then calls 0x55bea0. At that call, R8D=1 and R9D=0.
2. 0x55bea0 wraps a 16-byte descriptor in the same count-plus-entries packet family and calls 0x55bf70, forwarding the extra arguments.
3. In 0x55bf70, the incoming R8D value is stored as bit 0x100 in a 16-bit field at record+0x30 (instructions around 0x55c40b-0x55c421). The record-copy routine at 0x55a320 preserves that bit.
4. Other callers use the same drop family for inventory overflow. For example, the award routine calls 0x55bf70 with zero in that argument. Therefore identifying the function family alone is insufficient: not every physical bag is necessarily a deliberately left item.

**Interpretation:** this is a strong candidate for distinguishing left/traded loot, but the marker could have broader multiplayer-sharing meaning. We have not proved it is exclusive to player-left items, or that Seamless preserves it on the receiving PC. Creator/context fields should be studied alongside it.

### Existing pickup hook can see more metadata

The award call returning to RVA 0x55ce2e is inside the queue-processing routine starting at 0x55cd10. Its local record is copied before the award function is called. Static stack relationships show:

| Observation | Relative to record base | Relative to award packet |
|---|---:|---:|
| Opaque map-item handle/type value | +0x00 | -0x40 |
| Candidate block/frame identifier | +0x18 | -0x28 |
| Candidate pickup-record XYZ | +0x1c | -0x24 |
| Raw 16-bit record flags, including candidate 0x100 | +0x30 | -0x10 |
| Count-plus-item award packet | +0x40 | +0x00 |
| Three-word notification/creator-context region | +0xe4 | +0xa4 |

The position/block candidates are written by the drop-building path around 0x55c336-0x55c35d. The producer also writes context values around +0xe4/+0xe8 from game/player data. Do not label these as Steam IDs or a verified owner identity without tracing their meaning.

The candidate raw record flags are **not the R9 flags our current log records**. R9 is derived later from another handle/type field and notification choices. Our existing flags<=3 acceptance cannot stand in for the new provenance check.

A prospective read-only probe can snapshot the prefix during the existing detour, before the original award consumes the object. It should require the exact guarded caller, the expected output=packet+0xa4 relationship, readable bounded memory, and valid packet data. Arbitrary awards must never be probed by subtracting these offsets from their packet pointers.

This may avoid adding another hook for the first observation stage. A separate creation/Leave hook remains useful if the record lacks enough information on remote clients.

## Proposed framework

Use a small value-only SourceContext alongside the existing awarded packet:

```text
kind: world_treasure | enemy_drop | gathering | shop_purchase |
      player_left | scripted_reward | received_grant | unknown
confidence: verified_runtime | correlated | unknown
capture_path: guarded hook/caller identifier
raw_evidence: source handle/type, record flags, observed context
object_identity: session-scoped local handle + lifetime/generation, when known
static_identity: map, part/entity, lot table + row, shop row, when known
collector_location: current map/block + XYZ + coordinate-space label
object_location: record/map/frame + XYZ + coordinate-space label, when validated
creator_relation: self | other_player | nonplayer | unknown
items: existing resolved item IDs, quantities and equipment metadata
```

Classification, sharing policy, diagnostic output and static enrichment should be separate functions. A weak location match must never silently become verified ownership. Unknown is an explicit result. Record why a classification was made.

For the duplication fix, the collecting PC must recognize a left item regardless of who created it. Remembering only local Leave actions is insufficient. A true object identity with generation can supplement native flags; item ID, raw pointer, position or time window alone cannot safely identify it. Handles can be reused and are not automatically portable between PCs.

Source filtering can initially remain local, with delivery ID correlation in logs. Do not put source metadata into the item's unknown/gem words: they already carry equipment data. If metadata is later sent to peers, give it its own versioned representation.

## Location design

Keep these observations distinct:

- Collector position: where the originating player stood when collecting. It is useful diagnostic context and comparatively straightforward to add, but not the exact object's coordinate.
- Pickup-record position: the record's own coordinates. Validate whether they represent creation, ground-adjusted or current object position, and identify their map frame before calling them physical pickup coordinates.
- Static placement: map-part/chest position or enemy spawn position from generated assets. A moving enemy can die far from its spawn; a left item does not have a pre-authored MSB placement.

Always retain map ID and coordinate space with XYZ. Invalid/loading values should be absent, not zero or stale. Do not substitute the recipient's position for the original source location when an item is granted remotely.

For named locations, optionally build a read-only lookup from the actual loaded/generated randomizer MSBs, regulation tables and scripts. Retain one-to-many lot-to-entity relationships; do not index solely by resolved item ID or assume entity IDs are always populated. Part name plus map is often needed. A nearest-landmark label is approximate, especially across floors or inaccessible areas, and should be displayed as such.

## Recommended implementation order

1. Add shadow diagnostics only: source prefix/type/raw flags, creator-context candidates and collector location at the existing world hook. Keep current sharing decisions unchanged during observation.
2. Compare own-left, other-player-left and ordinary world/enemy pickups of the same item. Validate local and remote recipients, stacks, quantity splits, Leave Selected, and a warp/rejoin if the ground object survives. Include overflow-created bags to avoid conflating them with deliberate trades.
3. Once native evidence is consistent, classify PlayerLeft explicitly and exclude it before broadcast. Preserve valid world/enemy sharing. Add regression fixtures from anonymized observed records.
4. Add exact object coordinates and static lot/entity enrichment independently. Do not make map-data extraction or landmark lookup a prerequisite for the urgent duplication fix.
5. Only then replace item-ID-based gathering/script heuristics with validated source categories where evidence supports doing so.

## Current status

This research establishes a concrete metadata capture point and a candidate drop marker. It does not establish a completed player-drop exclusion, exact location API, or universal source classifier. No game was launched and no installed mod was changed. Existing 0.5.0-test.2 and uncommitted feature work remain as they were.

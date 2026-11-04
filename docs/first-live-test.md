# First live observer test — 2026-11-04

The user updated Seamless before this launch, created a vanilla-assets test character, and reported collecting the Finger Severer and other items.

Verified at runtime: `modengine2.dll`, `ersc.dll`, and `SharedLoot.dll` were loaded. No Easy Anti-Cheat module was returned by the module check, and no EAC/protected-launcher process was detected. The observer reported `MH_OK` at RVA `0x5618B0`. This verifies attachment in this particular configuration, not general compatibility.

The first snapshot contains four valid acquisition requests with five item entries:

| Raw ID | Goods ID | Name | Requested quantity | Caller RVA | Flags |
| --- | --- | --- | --- | --- | --- |
| 0x4000006a | 106 | Tarnished's Wizened Finger | 1 | 0x55ce2e | 2 |
| 0x400003e9 | 1001 | Flask of Crimson Tears | 3 | 0x56441c | 4 |
| 0x4000041b | 1051 | Flask of Cerulean Tears | 1 | 0x56441c | 4 |
| 0x40000067 | 103 | Finger Severer | 1 | 0x55ce2e | 2 |
| 0x40000064 | 100 | Tarnished's Furled Finger | 1 | 0x55ce2e | 2 |

Names were cross-checked against https://raw.githubusercontent.com/soulsmods/Paramdex/master/ER/Names/EquipParamGoods.txt. A local log snapshot is kept under ignored `work/live-tests/first-pickups.jsonl`.

Two caller paths/flag values are observable here. This is useful evidence for future classification, but it does not yet prove either path is exclusive to floor loot, nor that the flags themselves identify source. The flask awards may be initialization behavior. All requests remain unclassified, and requested quantity is not yet a confirmed inventory delta.

Next live observations: an ordinary gathering resource, an enemy loot pickup, and later a shop purchase/storage transfer to compare paths. No item sharing or grants are enabled.

## Varied collection follow-up

The user's next report was that they had collected a variety of items. The inspected snapshot then contained 23 acquisition requests, all with valid packet layouts, with no diagnostic-drop reports. It included repeated Rowa Fruit requests with quantities 1, 2, and 3; Thin Beast Bones; Smithing Stone [1]; Kukri x4; Root Resin; Lump of Flesh; Gold-Tinged Excrement; a Tree-and-Beast Surcoat (raw `0x1019f104`); and a Lordsworn's Greatsword (raw `0x002e3bf0`). Names were checked against Paramdex's Goods, Protector, and Weapon name tables. These entries establish that the observer sees consumable/material, armor, weapon, and multi-item request layouts in this run. They do not individually establish each item's provenance or successful quantity.

The two game caller paths continued to occur, with overlapping flag values. A separate direct caller from `ersc.dll` at RVA `0x23fb0` requested seven goods in the `0x407fde61`–`0x407fde6c` range (non-contiguous). These are mod-origin grants and must not be treated automatically as normal pickups or re-broadcast. Spectral Steed Whistle also appeared as a game-origin request; excluding only direct Seamless callers will not be a sufficient source filter.

Snapshot: ignored `work/live-tests/varied-pickups.jsonl`. The next controlled comparison should be a shop purchase followed by a storage withdrawal, one at a time, with item names reported by the user. Keep grants and sharing disabled until those distinctions are understood.

## NPC rewards, storage, and shop comparison

The user reported receiving Spirit Calling Bell and Lone Wolf Ashes from an NPC, depositing and withdrawing a Kukri, and purchasing a Telescope. They explicitly require that storage transfers must never share items.

The log then contained 25 requests, exactly two more than the prior snapshot:

- Spirit Calling Bell, raw `0x40001fde`, quantity 1.
- Lone Wolf Ashes, raw `0x40038a40`, quantity 1.

Both originated from `eldenring.exe` caller RVA `0x56441c` with flags 4. Names were verified against Paramdex. This path/flag also appeared for the flasks and Spectral Steed Whistle, so it is not an ordinary-pickup-only source marker.

No Telescope request (`0x400007f8`) was captured. The only Kukri request remains the earlier x4 world pickup; no additional Kukri call appeared for the reported storage transfer. This is evidence that those tested shop/storage operations bypass the observed award function, not a guarantee for all shop/storage variants or future adapters.

Requirements: exclude all storage transfers and shop purchases from v1 sharing; do not broadcast NPC/scripted awards or Seamless-origin grants by treating every observed award as a pickup. Ground/enemy/gathering provenance still needs validation before routing requests into the sharing core. Snapshot: ignored `work/live-tests/npc-shop-storage.jsonl`.

## Build 0.2.0 controlled Rowa grant

The user reported "I think it worked." The live log confirms one arm event, followed by a normal Rowa Fruit x1 request and exactly one diagnostic Rowa Fruit x1 request on the same game thread. A second normal Rowa Fruit x1 request about 19 seconds later had no extra diagnostic grant. No invalid grant context or dropped-diagnostic reports appeared. Snapshot: ignored `work/live-tests/rowa-grant-test.jsonl`.

This confirms the one-shot request path ran live and did not repeat on the second collection. The user's impression supports success, but exact before/after inventory counts have not been supplied, so successful inventory acceptance is not independently confirmed by this log. Automatic sharing remains disabled.

## Build 0.4.0 first successful Steam share

After the second PC switched to the correct `launch_sharedloot_steam.bat`, the host log reached one discovered member and one ready peer, with a running frame task and no send errors. The user then reported collecting Golden Rune [1] and the other player receiving it.

The host log captured raw item `0x40000b54`, quantity 1, on the ground/enemy pickup path and a matching `steam_pickup_sent`. It also sent one Rowa Fruit request in the same test period. The host's total sent counter was 2 with zero send errors and no incoming grants yet. Combined with the user's recipient confirmation, this establishes the first observed successful host-to-guest item share over the dynamic Steam backend. Recipient-side timing/counts were not independently read from its log. Reverse-direction sharing, equipment, exclusion behavior, and party changes still need live validation.

Snapshot: ignored `work/live-tests/steam-first-success.jsonl`. The older `sharing_eligible:false` log field is stale legacy diagnostic labeling in 0.4.0; actual sends are identified by `steam_pickup_sent` and counters. Correct this field in the next build without replacing a running DLL.

## Two-way sharing and missing Ruin Fragments

The user supplied the other PC's `diagnostics-20261104-134633-22772.jsonl` and confirmed receiving items and a weapon. Its final counters were 8 sent and 5 received; the local host's inspected counters were 5 sent and 8 received, with zero send errors. The host log contains the corresponding remote grants, including the weapon packet. This supports working bidirectional dynamic discovery/messaging and frame-based grant calls; inventory acceptance is supported by the user's report.

Three Ruin Fragment requests (`0x400006e0`, goods ID 1760), quantity 1 each, were captured in the client log at caller `0x56441c`, flags 0. None had a matching `steam_pickup_sent`; none appeared in the host's grant records. The cause was the v0.4.0 gathering filter's 20500–20999 material band, which excluded goods ID 1760.

Build 0.4.1 adds the specifically confirmed gathering item, with a regression asserting that the same item on NPC/scripted flags 4 or a mod/unknown caller remains excluded. It also fixes the stale eligibility log field. Windows source/router/adapter/Steam-mock checks passed, as did package integrity. A universal 0.4.1 update package was prepared; the running game/DLL was not replaced. Missed pickups are not replayed retroactively.

Snapshots are ignored under `work/live-tests/steam-client-varied-items.jsonl` and `steam-host-varied-items.jsonl`.

## Randomizer follow-up

After installing Shared Loot 0.4.1 and enabling the existing generated randomizer through the normal Mod Engine 2 profile, the user reports that sharing works with the randomizer on two PCs. This is user-reported live proof-of-concept evidence; exact coverage of every randomized acquisition source and full-playthrough behavior remains unverified. No new log trace was supplied with this confirmation.

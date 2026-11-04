# Shared Loot Steam test — build 0.4.1

This replaces the host/client LAN prototype. **Every participant uses the same DLL and INI.** Steam identities and current lobby membership determine the peer set; there are no configured IPs, network tokens, host/client roles, router ports, or extra passwords. Seamless still handles the game party and its ordinary password.

## Install on the other PC

1. Close Elden Ring on that PC and back up its saves and previous `shared-loot` folder. Use disposable Seamless characters with inventory room on both PCs.
2. Transfer `SharedLoot-Steam-0.4.1.zip`. Extract the **contents** into that PC's Mod Engine 2 root, beside `modengine2_launcher.exe`.
3. Replace `shared-loot/SharedLoot.dll` **and** `shared-loot/SharedLoot.ini`. Copy `config_sharedloot_steam.toml` and `launch_sharedloot_steam.bat` into the root. The previous 0.3.0 DLL cannot communicate with this build. Keep that PC's separately installed, updated Seamless and existing settings.
4. The new TOML assumes Seamless is at `ersc.dll` beside the launcher, as on this PC. If that PC keeps it under `SeamlessCoop/ersc.dll`, change only that path in the new TOML. Both PCs need matching Elden Ring/Seamless versions. The addon is locked to executable file version 2.7.2.0 and its inspected SHA-256; mismatches disable it.
5. Launch **`launch_sharedloot_steam.bat`** on both PCs. Use the new Steam launcher, not either old diagnostic/LAN launcher. This test loads vanilla assets and leaves randomizer/fasttorrent disabled.

```text
Mod Engine root/
  ersc.dll                         your existing Seamless dependency
  ersc_settings.ini                existing; unchanged
  modengine2_launcher.exe          existing
  modengine2/                     existing
  config_sharedloot_steam.toml     NEW
  launch_sharedloot_steam.bat      NEW
  shared-loot/
    SharedLoot.dll                 REPLACE on both PCs
    SharedLoot.ini                 REPLACE on both PCs
    STEAM-TEST.md
    THIRD-PARTY-NOTICES.txt
    manifest.json
    logs/                         created/preserved locally
```

No test mock DLL, Steam SDK DLL, source files, compiler, or Python belongs in the game/mod folders. The package contains only the seven explicitly listed addon/configuration/documentation files; the real Steam runtime is the game's existing installation.

## What is implemented

- Lobby lifecycle callbacks and a read-only membership-checked fallback discover peers. The addon uses the game's already initialized Steam interfaces; it does not take over the callback pump or change lobby metadata.
- Every participant can collect and send to every other compatible participant. An addon hello/ack handshake, lobby/session nonce, current-membership checks, and per-sender sequence numbers gate item messages.
- Ground, corpse, chest, and enemy-loot requests on the observed pickup path can share goods, weapons, armor, talismans, and Ashes of War, with quantities and packet metadata preserved.
- Vanilla gathering resources in the verified material ID band can also share. General scripted item-lot awards remain excluded. NPC gifts, scarab-style unique rewards, mod-origin grants, shop purchases, and storage transfers must not be rebroadcast. The source coverage remains empirical rather than a complete classification of every game event.
- Build 0.4.1 adds the confirmed gathering-path Ruin Fragment (goods ID 1760), which build 0.4.0 mistakenly filtered. It also corrects the `sharing_eligible` log field. Install the update on both PCs with the games closed; existing fragments are not retroactively replayed.
- A pinned recurring task registered in the game's frame scheduler services incoming bundles. **The receiver should no longer need to collect an item.** This frame integration and real cross-PC Steam coexistence are new and need this live test; local mocks cannot establish them.
- Ground copies stay available. There is no saved collection history, late-join catch-up, or guaranteed delivery across disconnect/crash. Pending messages expire/discard on session/readiness changes.

## Test steps

1. Load your test characters, host with either PC, and join the same Seamless world normally. No F8 arming should be required. Wait a few seconds after both have loaded.
2. Logs should show `hook_status:MH_OK`, `steam_available`, and `frame_task_registered`. Once loaded and joined, `steam_status` should show `lobby:true`, `world_ready:true`, `frame_active:true`, and `ready_peers:1` for two players. If no peer becomes ready, report the logs before collecting more items.
3. Note inventory counts. Player A collects a small gathering material or ordinary floor item while Player B stands still. B should receive the same eligible item/quantity without a pickup action. Verify inventory rather than relying only on popups.
4. Reverse direction: B picks something up while A stands still. Then test one ordinary weapon/armor pickup. Do one action at a time and report exact items/counts.
5. Verify that a storage deposit/withdrawal and a shop purchase do not generate gifts. NPC/scarab rewards remain Seamless's responsibility and must not get a second addon copy.
6. Later, switch which PC hosts by ending and recreating the Seamless session. Both installations remain identical. The addon should rediscover the new party; it does not make Seamless itself support seamless host migration.

Use F9 to pause addon sharing and F8 to resume if needed. Old F8 extra-fruit/F10 sending controls are inactive. Leaving/rejoining resets readiness and requires a new handshake. Do not use this experimental build on established saves; receiving real gear/key items may persist in the test character.

## Useful log meanings

- `steam_available`: required Steam interfaces were found.
- `steam_status` with no lobby: discovery has not identified a lobby containing the local Steam user. It will not send to arbitrary friends.
- Members but `ready_peers:0`: the other addon has not completed a compatible hello/ack, is not loaded into the world, or the Steam message route has failed. `hello_sent` and `send_errors` help distinguish those cases.
- `frame_task_disabled` / no `frame_active` after loading: stop and report; the new game-thread integration is not verified.
- `steam_pickup_sent`: an eligible bundle was queued for ready peers; the send status and remote log still matter.
- `steam_grant_request`: the game-thread grant routine was called. Inventory acceptance/capacity is verified in game, not by this log alone.

Normal logs omit player names, Steam IDs, lobby IDs, passwords, and IPs. No Steam message queue except this addon's dedicated channel is read; whole Steam peer sessions are not closed. Relay-only routing is not forced or claimed.

## Limits for this test

Real Seamless lobby callback delivery, channel coexistence, world-readiness offsets, and frame-task behavior are not proven by the mock tests. If the game closes/crashes or a guard disables the addon, stop and keep the log; do not bypass build checks. The previous DLL/configuration and fresh saves were backed up before this PC's installation.

Randomizer compatibility is not yet validated. The transmitted item is the resolved pickup, so no vanilla-location lookup is used, but all players must eventually use identical generated data. Set `gathering_materials=0` while investigating randomizers: material ID alone cannot prove a randomized scripted/scarab reward is a gathering node. Retain NPC/scarab/shop/storage exclusions.

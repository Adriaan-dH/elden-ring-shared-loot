# Two-PC Rowa Fruit test — build 0.3.0

Both PCs need SharedLoot.dll. The sending PC observes real pickups, and the receiving PC must grant the item locally. Seamless still loads separately and handles the actual co-op game. This is a two-player LAN prototype, not a host-only mod or a finished general-loot release.

**Important current limit:** incoming fruit is granted during the receiver's next ordinary item acquisition, not while standing idle. The receive queue expires after 60 seconds and is discarded on connection loss or F9. No game pointers are held for a background thread, and no late-join collection history is stored.

## Your PC — host

Codex installs this build into `<Mod Engine 2 folder>`, after backing up the previous observer and saves. Its ordinary `config_eldenring.toml`, `launchmod_eldenring.bat`, and Seamless settings stay unchanged.

The legacy LAN package listens on the private address supplied to the packaging script, TCP port `49231`. It starts with sending allowed, but sharing remains inactive until F8 is pressed in the game. Runtime addresses and generated connection tokens belong only in local packages/configuration; do not commit them. This manual host/client setup is being replaced by the Steam/session-based design in `networking-investigation.md`.

## Other PC — client

1. Close Elden Ring. Back up its `%APPDATA%\EldenRing` saves and existing Mod Engine configuration. Use a disposable Seamless character with room in inventory, on both PCs.
2. Have the same Elden Ring build and updated Seamless version on both PCs. This DLL is locked to the executable inspected here (file version 2.7.2.0, SHA-256 `f9118fea2cfd8fa3901e8060bfadb1552cb2471771ded8a5b662c4e298ffb328`). A mismatch disables the hook; do not bypass it.
3. Transfer `SharedLoot-client-0.3.0.zip` to the second PC. Extract its contents into that PC's existing **Mod Engine 2 root**, beside `modengine2_launcher.exe`. This is the client package, not the host package. It includes a matching generated network token and the correct host address; no Python or compiler is needed.
4. Confirm the layout below. Keep that PC's own Seamless installation/settings. The package does not contain Mod Engine, Seamless binaries, or game files. If its Seamless DLL lives in a subfolder, edit only the `ersc.dll` entry in the new `config_sharedloot_diagnostics.toml` to point to that existing DLL (for example `SeamlessCoop/ersc.dll`). Preserve the other entry.
5. Keep the same Seamless session password on both PCs as usual. SharedLoot's separate network token already matches between the two packages; it is not your Steam or Seamless password. Client `send_pickups=0` is intentional for the first one-way test.

```text
Mod Engine root/
  modengine2_launcher.exe                 existing
  modengine2/                             existing
  ersc.dll                                existing, or its configured subfolder
  ersc_settings.ini                       existing
  locale/                                 existing Seamless resources
  config_eldenring.toml                    existing; unchanged
  config_sharedloot_diagnostics.toml       from package
  launch_sharedloot_diagnostics.bat        from package
  shared-loot/
    SharedLoot.dll
    SharedLoot.ini
    MANUAL-TEST.md
    THIRD-PARTY-NOTICES.txt
    manifest.json
    logs/                                 created at launch
```

The new test profile uses vanilla assets on both PCs: randomizer and fasttorrent are disabled. Future randomizer support must use matching generated data and the actual resolved pickup; this test does not validate it.

## Launch and enable

1. Start Steam on both PCs. Launch **`launch_sharedloot_diagnostics.bat`** on each PC, not normal Steam Play. The Mod Engine/Seamless launch path is required.
2. Load disposable characters and join the same Seamless session normally. This prototype does not automatically verify Seamless party membership: do not enable it while playing separate worlds or characters.
3. On each PC, with Elden Ring focused, hold **F8 for about half a second**, then release. This enables the LAN test. The old extra-fruit F8 test is disabled in these packages.
4. Each newest `shared-loot\logs\diagnostics-*.jsonl` should show `hook_status`=`MH_OK`, `network_available`, and eventually a `network_status` with `state:4`. State 1 means waiting/connecting, 2 authenticating, 3 connected but not enabled on both PCs, and 4 ready. No receiving or sending happens until both are enabled.

If Windows asks about network access, handle the prompt yourself for your trusted private/home network. No router port forwarding is needed. If it does not connect, check the host address, both INI tokens/port, and private-network inbound access for TCP 49231/Elden Ring. Codex does not change firewall settings. This prototype sends its token and item quantity over plain TCP and is for a trusted LAN, not public internet exposure.

## First test — host sends, client receives

1. Confirm both inventory counts. The host starts with outbound sending on; the client starts with it off.
2. Host gathers Rowa Fruit, noting the normal quantity N. The host should receive only N locally. Its log records `rowa_sent`; the client log should show `received` increasing and `pending` becoming 1.
3. Within 60 seconds, the client gathers Rowa Fruit, noting its own normal quantity M. This acquisition services the pending message on the game thread. Expected client increase: **M + N**. Its own pickup is not sent back, because client sending is off.
4. Client gathers another bush. With no pending message, it should receive only that bush's normal amount. No extra looping grant should occur.

Tell Codex both PCs' counts and bring the client's newest log back if needed; the host log can be read locally. A `rowa_remote_grant_request` means a call was issued, not independent proof the inventory accepted it. Stay below inventory capacity for this test.

## Reverse direction

After the first test has no pending messages, press **F10 once on each PC**: host sending turns off, client sending turns on. Logs record `outbound_mode`. Repeat the test in reverse: client gathers first, then host gathers to apply the incoming amount. F10 changes sending only; it does not disconnect or turn off receiving.

## Stop or leave a session

Press **F9 on both PCs before leaving the Seamless session, changing character, or returning to the title screen**. This disables the bridge and discards queued rewards. Session membership/character tracking are manual in this prototype. Restarting the game also starts with sharing disabled until F8.

Only Rowa Fruit is sent by this test. NPC and scarab rewards, shop purchases, and storage transfers must not be shared; all general-item routes remain disabled. A storage withdrawal itself will neither send nor service a pending reward. Ground items stay available. There is no offline history or late-join catch-up; dropped/expired messages are not replayed.

To return to the original setup, close the game and use the original launcher. The previous observer and fresh saves are backed up locally. Keep test saves separate from vanilla online play.

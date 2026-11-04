# Elden Ring Shared Loot

Shared Loot is a free, open-source companion mod for **Seamless Co-op**. When one player collects an eligible item, everyone else currently connected and running Shared Loot receives the same item and quantity.

**Early proof of concept — v0.4.1.** Two-way sharing has worked in our two-PC tests, including a randomizer session. There are still potential issues and untested cases. We will improve the mod as we play, test, and collect bug reports.

## Installation (Mod Engine 2)

Install Seamless Co-op separately on every PC. Everyone needs the same Shared Loot version and must launch through Mod Engine 2. This setup has been tested with **Mod Engine 2**; Mod Engine 3 has not been tested.

1. Download the release ZIP and extract its `shared-loot` folder beside `modengine2_launcher.exe`.
2. Open your existing `config_eldenring.toml`. Add `"shared-loot/SharedLoot.dll"` to the existing `external_dlls` list under `[modengine]`. Keep your other DLL entries.
3. Launch your usual `launchmod_eldenring.bat` on every PC, then join through Seamless as normal.

For a setup with Seamless at the launcher root:

```text
ER Mod Engine/
  modengine2_launcher.exe
  launchmod_eldenring.bat
  config_eldenring.toml
  ersc.dll
  ersc_settings.ini
  shared-loot/
    SharedLoot.dll
    SharedLoot.ini
    README.md
    LICENSE.txt
```

```toml
[modengine]
debug = false
external_dlls = ["ersc.dll", "shared-loot/SharedLoot.dll"]
```

If your Seamless DLL is inside a subfolder, use `"SeamlessCoop/ersc.dll"` instead. Paths above are relative to the Mod Engine launcher folder.

**Do not add Shared Loot to the `mods` list.** That list is for game assets. Keep your existing randomizer and other asset-mod entries there. Keep any extra DLL entries required by those mods, too. Shared Loot does not replace your randomizer setup.

## How it works

Shared Loot observes the game's item-award function. For acquisitions accepted by its pickup filter, it sends the actual awarded item IDs, quantities, and equipment metadata to the other ready participants through Steam messaging. Each receiving PC grants the items on a game frame task. Session checks and sequence numbers reject repeated network messages, and received grants are prevented from broadcasting again.

- Any participant can collect and share; it is not host-only. No IP addresses or extra Shared Loot password are needed.
- Other players' world pickups stay available. Picking up another copy deliberately shares another reward.
- It does not synchronize whole inventories, remove ground items, or catch up players who join later.
- NPC/scarab rewards already shared by Seamless should not be broadcast again. Shop purchases and storage transfers should not be shared. These exclusions use observed acquisition paths and still need wider testing.

## Randomizer and known limitations

Everyone must use matching generated randomizer files. Shared Loot sends the resolved awarded item, so it does not need to reconstruct loot from a seed. A two-PC randomizer test worked, but this is **experimental compatibility**, not a guarantee for every item or reward source.

Gathering currently uses a vanilla-material whitelist plus Ruin Fragments. Randomized rewards outside that whitelist can be missed on the gathering path; already-shared scripted rewards that match it may be duplicated. We are still improving source detection.

The addon is locked to the verified game executable and can stop working after a game update. Only two players have been tested in-game. The addon currently rejects lobbies above eight members; Seamless has its own player limit.

Back up your Seamless saves before testing. Keep `[diagnostics] enabled=1` in `SharedLoot.ini`; this currently enables the addon worker as well as its logs. Logs are created in `shared-loot/logs`. F9 pauses sharing and F8 resumes it.

## Feedback and license

If something is missing or duplicated, report the item, where/how it was acquired, who collected it, who received it, and the approximate time. Include Shared Loot versions and relevant logs from both PCs after checking them for private information.

Code and issue reports: https://github.com/Adriaan-dH/elden-ring-shared-loot

Shared Loot is licensed under the MIT License. MinHook is included under its own MIT license; its attribution is included with the release. Seamless Co-op, Mod Engine, the randomizer, and game files are separate downloads.

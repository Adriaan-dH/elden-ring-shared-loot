# Manual observer installation and first game test

This build observes item-award requests. **It does not share items yet.** All source categories are logged as `unclassified`, including any shops, quests, or scripted rewards that reach this function. A request is not proof of a successful inventory addition. The next game test is to find out which real acquisitions reach this hook and how their caller paths differ.

The DLL is locked to the exact installed executable inspected on 2026-11-04 (file version 2.7.2.0, SHA-256 `f9118fea2cfd8fa3901e8060bfadb1552cb2471771ded8a5b662c4e298ffb328`). A different build or a changed hook entry produces a disabled status instead of attaching. No game has been launched during development.

## Install manually

1. Close Elden Ring. Back up your `%APPDATA%\EldenRing` folder somewhere outside that folder. Use a new disposable Seamless character for this test: the new profile loads vanilla assets, with your randomizer disabled.
2. Extract `SharedLoot-observer-0.1.0.zip` to a temporary folder.
3. Copy its **contents** into your existing `<Mod Engine 2 folder>` folder. The package contains only new names; do not replace your original `config_eldenring.toml`, `launchmod_eldenring.bat`, or Seamless files. If those new diagnostic filenames already exist from an earlier test, replace only those diagnostic files.
4. Confirm the layout below. Keep `ersc.dll`, `ersc_settings.ini`, `locale`, and the Mod Engine files in their current positions. The package does not include them.

```text
ER Mod Engine/
  modengine2_launcher.exe             existing
  modengine2/                         existing
  ersc.dll                            existing
  ersc_settings.ini                   existing; keep your settings
  locale/                             existing
  config_eldenring.toml               existing; unchanged
  launchmod_eldenring.bat              existing; unchanged
  config_sharedloot_diagnostics.toml   NEW
  launch_sharedloot_diagnostics.bat    NEW
  shared-loot/                        NEW
    SharedLoot.dll
    SharedLoot.ini
    MANUAL-TEST.md
    THIRD-PARTY-NOTICES.txt
    logs/                             created when loaded in Elden Ring
```

You do not need to add this DLL to the asset `mods` list or change your existing launcher. The new TOML loads it through `external_dlls`, after `ersc.dll`. No Python, compiler, development files, or networking setup is needed to run this observer. Log files go alongside the installed DLL, not to the WSL repository. OneDrive may sync these local logs.

## First test — one PC only

1. With Steam running, launch **`launch_sharedloot_diagnostics.bat`**. Use the normal Seamless/Mod Engine flow with EAC disabled; do not launch this test through the standard Steam Play button.
2. Confirm the Seamless startup/version indication appears. Create or load your disposable test character. Wait at least five seconds after launch for the observer to initialize.
3. Check `shared-loot\logs` for the newest `diagnostics-*.jsonl` file. Filenames use UTC. Its first lines should include `startup` and a `hook_status` with `status` equal to `MH_OK`.
4. Pick up one ordinary ground item. Note its name and quantity and the time. Check for a new `award_request` line, recording raw item ID and quantity.
5. Collect one enemy's item drop and note the item and quantity. If possible, also collect a gathering resource, buy one cheap item, and move one item from storage. Do one action at a time so logs can be compared. Do not assume the observer has filtered these yet.
6. Quit normally, then give Codex the resulting log file or its contents and your action order. Do not send your Seamless password or save files. Floor/enemy/gathering/shop/storage results will tell us what to classify or investigate next.

This first test does not require the second PC. It should not change loot behavior. Another player's copy remains collectible because the observer does not suppress pickups.

## If the observer disables itself

- `config_missing_or_disabled`: ensure `SharedLoot.ini` is beside the DLL, with `[diagnostics]` and `enabled=1`.
- `executable_hash_mismatch` or `loaded_image_mismatch`: stop this test and report the log. Do not edit or bypass the compiled build check.
- `award_prologue_changed_or_already_hooked`: another component has changed that function. Report this result; do not remove Seamless or force a second hook just to get past it. We need to inspect hook compatibility.
- `eac_module_present`: exit and verify the modded launch path.
- A different `hook_status`: report the status string.
- No log: check that the new batch file and TOML are in the Mod Engine root, and the DLL and INI are under exactly one `shared-loot` folder. If launch fails or crashes, stop and report the behavior.
- `MH_OK` but no award lines: that is useful evidence. Tell us which actions you tried; another acquisition path may be in use.
- `dropped_diagnostics`: the logger's bounded queue overflowed or was busy. This is a diagnostic limitation, not a lost game item.

## Disable/remove

Exit the game. Use your original `launchmod_eldenring.bat` to return to your existing mod profile. To remove the observer, remove only `shared-loot`, `config_sharedloot_diagnostics.toml`, and `launch_sharedloot_diagnostics.bat` that you copied in this procedure. Disabling `[diagnostics] enabled=0` keeps the DLL loaded but attaches no observer hook.

## What remains after this test

Validate acquisition provenance and actual awarded quantities/capacity behavior; implement and test local grants; then connect two PCs and identify actual session members. Neither the synthetic networking tests nor the static inspection establish those game behaviors.

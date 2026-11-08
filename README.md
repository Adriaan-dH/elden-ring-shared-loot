# Elden Ring Shared Loot

Shared Loot is a free, open-source companion mod for Seamless Co-op. It shares eligible world loot and shop purchases with connected players running the mod. Any player can collect and share.

The current development build is experimental. Shop delivery, NPC dialogue buffering and player-dropped item filtering are still being tested in co-op.

## Install with Mod Engine 2

1. Install Seamless Co-op on each PC and use the same Shared Loot build.
2. Extract the `shared-loot` folder next to `modengine2_launcher.exe`.
3. Add `"shared-loot/SharedLoot.dll"` to `external_dlls` in `config_eldenring.toml`. Keep the existing Seamless Co-op and other mod entries.
4. Launch with `launchmod_eldenring.bat` and join through Seamless Co-op.

```toml
[modengine]
external_dlls = ["ersc.dll", "shared-loot/SharedLoot.dll"]
```

Paths are relative to the launcher. If your Seamless DLL is in a subfolder, use that path in the list.

## What gets shared

- Eligible ground loot, enemy drops and items on an additional gathering allowlist.
- Completed shop purchases. The buyer pays and peers receive the quantity the buyer obtained.
- Shared loot waits through death, loading and open menus. The mod checks the recipient's inventory after applying each delivery.

The source filters exclude storage transfers, scripted rewards already handled by Seamless Co-op and marked player-dropped items. Each player can still collect their own world pickups. Pending gifts are kept in memory for the current session and character.

Randomizer players should use matching generated files. Randomized gathering rewards may fall outside the allowlist. A game update may require a new Shared Loot build.

## Settings and feedback

Edit `shared-loot/SharedLoot.ini` and restart the game. Shop purchases and supported gathering items are enabled by default. To report a missed or duplicate item, enable logging on both PCs and include the item, what each player was doing and the relevant files from `shared-loot/logs`.

[Report an issue](https://github.com/Adriaan-dH/elden-ring-shared-loot/issues) · [MIT license](LICENSE) · [Development notes](CONTRIBUTING.md)

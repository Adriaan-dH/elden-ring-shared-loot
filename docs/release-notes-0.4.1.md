# Shared Loot v0.4.1 — Early proof of concept

This is the first public experimental release of Elden Ring Shared Loot, a free and open-source companion mod for Seamless Co-op.

Eligible pickups are shared with the other currently connected players running the addon. Two-way item/equipment sharing has worked in two-PC tests, and a randomizer session has now been reported working too. This is a proof of concept with potential bugs, not a fully validated playthrough release. We will keep improving it as we play and test.

## Download and install

Download **SharedLoot-0.4.1-experimental.zip** from the release assets. It contains one folder:

```text
shared-loot/
  SharedLoot.dll
  SharedLoot.ini
  README.md
  LICENSE.txt
```

Extract beside the Mod Engine 2 launcher. Add `"shared-loot/SharedLoot.dll"` to the existing `[modengine] external_dlls` list, preserving Seamless and other DLL entries. Launch with your normal `launchmod_eldenring.bat` on every PC. Full path examples are in the included README. Tested with Mod Engine 2.

## Current limits

- Randomizer source filtering is incomplete; some gathering/scripted rewards may be missed or duplicated.
- Other players' world pickups remain available; another collection intentionally shares another copy.
- No inventory synchronization or late-join catch-up.
- Only two PCs tested live. Addon lobbies above eight members are rejected; Seamless has its own limit.
- Exact game-executable compatibility checks remain in place. A game update may require a mod update.

Back up saves before testing. Please report missing items, extra copies or crashes with acquisition details and relevant logs from the participating PCs.

Licensed under MIT, with MinHook attribution included. Seamless, Mod Engine, randomizer and game files are not bundled.

<!-- Publication draft only: mark this as a GitHub pre-release after review. Nothing has been committed, pushed or published during preparation. -->

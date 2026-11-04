# Controlled local grant test — build 0.2.0

No multiplayer sharing is active. This build adds an explicitly armed local test using the game's normal acquisition thread.

1. Launch `launch_sharedloot_diagnostics.bat` and load the disposable character used for the observer tests. Randomizer and fasttorrent remain disabled.
2. Find a Rowa Fruit bush. Check your current fruit count; leave room for at least two more fruit in inventory.
3. With the game focused, hold F8 for about half a second and release it. This arms the test for 120 seconds; nothing is granted immediately.
4. Gather Rowa Fruit. Expected inventory increase: the normal pickup quantity **plus one extra fruit**. Check inventory, not just the pickup popup.
5. Gather another Rowa Fruit. Only the normal amount should be added. The extra attempt is limited to once per game launch, even if F8 is pressed again.
6. Report before/after counts for both pickups. Codex can read the log directly.

An accepted extra item is real and may persist in this character's Seamless save. Use the disposable character. If F8 conflicts with another software hotkey or does not arm, report that.

## Implementation and logs

`[grant_test] enabled=1` is required in `shared-loot/SharedLoot.ini`, along with loaded Seamless and matching executable/prologue checks. An unused arm expires after 120 seconds and can be armed again. Only a single-entry Rowa Fruit call on the observed gathering path can trigger it. NPC bell/ashes rewards, other items, direct Seamless calls, and storage operations cannot trigger this test.

The callback forwards the normal pickup first, then calls its trampoline with a separate one-item packet and a local copy of the observed notification context. Game pointers are not retained for a worker thread. A recursion guard surrounds the operation, and the original packet is unchanged.

Logs record `grant_test_available`, `grant_test_armed`, and `grant_test_request`. A grant-test record with `packet_valid:false` means notification context could not be read and no extra call occurred. A valid record confirms a call was issued, not that inventory accepted it; the game test must establish success.

## Filtering status

Direct Seamless calls are labeled `excluded_mod_grant`; the observed NPC/scripted path with flags 4 is `excluded_scripted_reward`. Ground/enemy calls are candidates, and the general gathering/reward path remains ambiguous. **All paths still have `sharing_eligible:false`.** NPC and scarab rewards must not be broadcast again because Seamless already shares them. This build does not claim to identify all such variants.

Shop purchases and storage transfers remain excluded requirements. Their tested Telescope/Kukri cases bypassed the observer, but future adapters must enforce these exclusions rather than infer pickups from inventory increases.

To return to observer-only mode, close the game and set `[grant_test] enabled=0`. Never replace a loaded DLL. The normal Mod Engine configuration and Seamless settings are unchanged.

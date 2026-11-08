# Player-left item filtering — v0.5.0-test.3

## Implemented behavior

World pickups now carry a value-only source snapshot into the sharing decision.
The exact-build queue record contains an object handle and 16-bit flags. Native
inventory Leave sets flag 0x100; the native award-overflow drop path passes zero.
Marked player-left pickups are excluded before Shared Loot creates any network
delivery. The collector still obtains their normal pickup. This applies without
checking which player originally dropped it, the resolved item ID, or a timer.

The game owns the dropped object's lifetime and its marker. Shared Loot reads it
afresh at collection, before the native award consumes the record. It does not
retain pointers, keep a time-limited recently-dropped list, or treat reused handles
as the same object. Hours of delay and unrelated pickups cannot expire an addon
cache because there is no such cache. When the game removes a ground object,
Shared Loot does not recreate it. There is no cross-restart ground-item database.

No positions, creator identities, or new Steam messages are added. Equipment
metadata and quantity are untouched. Existing purchase sharing, script exclusions,
received-grant echo suppression and buffered delivery remain in place. This is a
world-pickup versus player-left classifier, not a complete treasure/enemy/item-lot
resolver; the gathering whitelist is still a separate existing limitation.

## Safety and diagnostics

The existing exact game-executable hash gate remains. The loaded world-award call
instructions are also checked, and the packet/notification pointer relationship
must match the verified record layout. Prefix reads use bounded ReadProcessMemory.
If a world source cannot be read or validated, the extra broadcast is blocked;
the normal pickup is never blocked. Source classification works with logging off.

With logging enabled, award_request includes pickup_source, object_handle,
record_flags and sharing_eligible alongside the exact item bundle. A player-left
pickup should show player_left_marker and sharing_eligible:false. An unreadable
source shows world_source_unavailable. Object handles are diagnostic local values,
not portable peer IDs or persistent identities.

The native marker's meaning is supported by disassembly, but Seamless propagation
and live left-item pickups are not yet validated. Automated tests simulate the
record format; they do not establish that remote ground objects preserve it.
This is a local test candidate, not a new published release.

## Install for the two-PC test

Install the same test build on both PCs using the Mod Engine instructions in the
README. Set `[logging] enabled=1` in each PC's `SharedLoot.ini` before starting
the game so the pickup source is recorded on whichever PC collects it.

Keep the same normal Mod Engine configuration and launcher on both PCs:

    external_dlls = ["ersc.dll", "randomizer/dll/RandomizerCrashFix.dll", "shared-loot/SharedLoot.dll"]

Retain the randomizer DLL entry only if that installation uses it, as before.
The shared-loot folder belongs next to config_eldenring.toml and the normal BAT
launcher. It is loaded through external_dlls, not the asset mods list. No new BAT
files or IP settings are needed. Use matching generated randomizer files.
Both PCs should use test.3, even though the network protocol is unchanged from test.2.

## Live test matrix

Use expendable items and record both inventory counts before and after each step.
An ordinary world pickup must still give its quantity once to the other player.
Then test these cases, including multiple-item stacks and a weapon:

1. A leaves an item, A collects it: only A regains it; B gets no extra grant.
2. A leaves an item, B collects it: only B gains it; A gets no extra grant.
3. Repeat with B leaving, both B collecting and A collecting.
4. Leave an item, play elsewhere for a long period, then collect it while the
   original ground object still exists. Repeat after a load/warp if it survives.
5. Collect ordinary world/enemy loot of the same item ID: it must still share.
6. Move items into/out of storage: no broadcast. Confirm purchases still share.
7. If practical, test a capacity-overflow bag separately: it must not be silently
   classified as a deliberate player trade merely because it is a runtime bag.

For any failure, retain BOTH PCs' shared-loot/logs JSONL files and describe who
left/collected the item and approximate time. Especially check player_left_marker
versus world_pickup on the collecting PC. A missing marker on a remote pickup
requires more native/Seamless investigation; it must not be masked with a cooldown
or item-ID blacklist. Do not consider delayed or remote collection proven until
these live tests pass.

## Local validation

12 portable tests pass, including exhaustive 16-bit marker tests, delayed
collection without cache expiry, owner-independent policy, handle reuse, copied
evidence after native-record mutation, unreadable records, bad pointer relations,
non-world ABI exclusion, and integration with the existing acquisition allowlist.
Windows x64 build and native tests pass. The Windows INI/logging, MinHook/game
profile diagnostics and mock Steam bidirectional messaging checks pass.

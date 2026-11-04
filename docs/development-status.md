# Development status — 2026-11-04

## Current review candidate: 0.4.1

Two-way sharing of items and equipment was observed on two PCs. The user now reports successful sharing with the randomizer as well. This establishes a working proof of concept, not complete source compatibility or a validated full playthrough. Ruin Fragment filtering and eligibility logging were corrected in 0.4.1.

The first-public-release candidate now uses an addon-only four-file ZIP, a basic installation README and an MIT project license with MinHook attribution. First-party C++ was formatted and commented without changing executable tokens (apart from equivalent string splitting/macro wrapping). Windows and Linux checks passed after rebuilding. No stage/commit/push/publication was performed; user review is next.

The entries below are historical snapshots of what was known at each earlier test, not the current validation status.

## Build 0.4.0 update — current

Live startup verification after installation: Elden Ring process loaded `modengine2.dll`, `ersc.dll`, and the 0.4.0 `SharedLoot.dll`; no EAC module/protected launcher was detected. The acquisition hook reported `MH_OK`, real Steam interfaces initialized, the new frame task registration returned true, and `frame_active:true` was observed at the title/loading state. `lobby:false` and `world_ready:false` were expected before joining/loading. Cross-PC discovery, world readiness, and actual idle grants are still pending the updated second PC's test. Previous installed observer/configuration and fresh saves were backed up before launching; ordinary Mod Engine/Seamless settings were unchanged.

The manual LAN bridge is replaced for the current package by dynamic Steam lobby discovery and identity-addressed messages on a dedicated addon channel. Both PCs use the same DLL/INI; no IP, host/client role, or token is configured. The existing Steam client and callback pump are reused. Lobby callbacks plus a read-only fallback require local lobby membership; version hello/ack, loaded-world readiness, per-session nonces, per-sender sequence numbers, and heartbeat/membership checks gate messages. IDs/names/addresses are omitted from ordinary logs.

The item payload now supports multi-item bundles across goods, weapons, armor, talismans, and gems/Ashes of War, preserving quantities and metadata. Only the observed world-loot path and the vanilla gathering band are eligible. NPC/scarab/scripted reward paths, shops, storage, and direct Seamless calls remain excluded; randomizer provenance beyond the ground/enemy path is unverified.

A persistent FD4 frame task is implemented at task group 117 to service received items without another pickup. Its registration function was uniquely signature-scanned at RVA 0xeb5540 and disassembled in the locked executable; the registered task ABI is locally tested. Manager/player pointers are re-read and checked each frame; no worker thread holds them. The module/task remain pinned to process exit. This is new live integration, not something the mocks can validate.

Tests cover three symmetric peers, bundle/metadata preservation, duplicate/invalid message rejection, membership/load transitions, Windows callback/task ABI, broad grant adapter, and the production Steam adapter against test-only native Steam exports. The mock Steam DLL is isolated under build output and is never packaged or installed. See `docs/steam-test.md` for live acceptance criteria. Prior status sections below are historical.

## Build 0.2.0 update

The first live observer session is recorded in `docs/first-live-test.md`. The current build adds observed-source hinting with known scripted/mod-call exclusions, while all production broadcast paths remain disabled. It also adds an opt-in F8 one-shot local Rowa Fruit grant, performed synchronously after a matching gathering call on the current game thread. This supersedes the earlier observer-only limitations below: a controlled local grant request is now implemented, but not yet confirmed by a live inventory test.

Windows and WSL tests passed for source exclusions and the diagnostic allowlist; Windows synthetic tests also passed for the one-shot grant, original packet preservation, one-item quantity, context copying, and recursion guard. Existing core, packet, hook, file-hash, and socket tests passed. The installed previous observer and fresh test saves were backed up before deployment. Relaunch of build 0.2.0 loaded Mod Engine, Seamless, and SharedLoot; its log reported `grant_test_available` and `MH_OK`. Follow `docs/grant-test.md` for the pending live test.

NPC and scarab rewards are excluded requirements because the user reports Seamless already shares them. The shared general reward/gathering path is ambiguous, so it remains blocked for automatic sharing; the controlled Rowa test is a separate explicit diagnostic action. Shop and storage operations remain excluded requirements. No multiplayer transport is connected to the game adapter.

## Implemented

- Portable C++17 shared library with a C ABI for a future game adapter.
- Windows x64 observer DLL. When loaded into `eldenring.exe`, a worker checks configuration, executable SHA-256, image metadata, and unchanged function-entry bytes before enabling an experimental acquisition observer. In other hosts it does not auto-start. The observer does not grant items, mutate pickup state, or perform networking; MinHook changes executable bytes in memory to install its detour.
- Floor/enemy source filters at the adapter boundary; actual game classification is still missing.
- Quantity-preserving synthetic grants, session-local origin/sequence identities, and ordered-delivery deduplication.
- Synchronous grant suppression to avoid rebroadcast loops.
- Bounded outgoing/incoming queues and retryable failed grants.
- Python loopback TCP diagnostic relay and mock-inventory peers using the native library through ctypes.
- A WSL build script for the Windows DLL and native test executables, using a locally downloaded compiler.
- Read-only offline executable inspection and a build-locked profile; bounded acquisition-packet snapshots and JSONL logs with caller locations. All real game sources are still unclassified.
- A separate manual-test Mod Engine 2 profile and an observer ZIP packaging script. Existing game/mod files remain unchanged.

## Verification

- Ubuntu WSL: CMake/GCC build with warnings treated as errors; native core tests passed.
- Ubuntu WSL: TCP tests passed, including separate peer subprocesses, deliberate repeated loot, no catch-up, and invalid handshake/origin rejection.
- Windows: cross-compiled `core_tests.exe` loaded the Windows DLL and passed the native core suite.
- Windows: all three Python socket tests passed against the Windows DLL, including separate Windows peer subprocesses, duplicate delivery/no catch-up, and invalid token/origin rejection.
- Windows: the exact native detour passed synthetic argument/return-value and packet-snapshot tests, including repeated enable/disable. This does not establish compatibility with Seamless's hooks.
- Windows: the DLL's BCrypt SHA-256 verifier accepted the inspected installed executable and rejected a mismatching fixture and null path, without starting the game.
- Linux and Windows: acquisition-packet decoding tests passed for truncated buffers, invalid counts, and metadata preservation.

These are synthetic tests and static file inspection. No game has been opened, no real acquisition or item grant has been tested, and the DLL has not been installed into the user's Mod Engine folder. The next step is the one-PC observer test in `docs/manual-test.md`.

## Integration contract and limitations

`SL_Capture` is called after a successful eligible local pickup. The collector already owns that reward; only other peers need a grant. `SL_NextOutgoing` exposes resolved item events for a future transport. `SL_Receive` queues incoming events; `SL_ApplyNext` invokes a future item-grant callback from the game thread.

All operations on one context must occur on the same thread. Network workers must hand messages to that thread. The suppression guard covers synchronous callbacks only; an adapter that defers acquisition notifications must retain explicit remote-origin metadata as well.

Each origin must be unique per connection/session. Delivery must be ordered and reliable per origin. State is intentionally session-only, with no crash recovery, disconnect catch-up, or persistent inventory synchronization. New sessions must not reuse identities with reset sequence numbers. The core supports up to eight remote origins per context.

Item IDs are opaque resolved identifiers. The diagnostic quantity bound is 1–999; this is not a verified in-game item database or per-item capacity policy. The eventual adapter must preserve item category and equipment metadata where necessary, validate items against the installed game's data, and obey inventory/storage capacity. Queue saturation returns an explicit error; the integration must report or handle it rather than silently dropping pickups.

The Python transport is a loopback-only test harness, not an authenticated Seamless session transport. Its token is a diagnostic handshake, not encryption. It is not intended for public or LAN deployment. It does not use Steam, query session membership, replay history, or implement reconnect delivery guarantees.

## Next work

1. Run the packaged observer manually on a disposable Seamless character. Establish whether it attaches alongside Seamless and records floor/enemy acquisitions, then compare other acquisition sources.
2. Use observed caller paths to implement source classification; establish actual granted quantities and inventory-capacity behavior rather than treating requests as successful pickups.
3. Implement and verify a local grant, including randomized resolved items and equipment metadata.
4. Connect the game adapter to an appropriate peer transport, validating actual party membership. Then test on both PCs.

The Steam app-version mapping and installed Seamless release number remain unverified. The observer instead uses the exact local executable hash and header/prologue checks. See `docs/hook-research.md` for offline findings.

## References

- Seamless author/release information: https://www.nexusmods.com/eldenring/mods/510
- Public game bindings: https://github.com/vswarte/fromsoftware-rs
- Existing item-grant tooling: https://github.com/Nordgaren/Elden-Ring-Debug-Tool
- Mod Engine 2 configuration: https://ersc-docs.github.io/seamless-modding/
- Compiler download metadata: https://ziglang.org/download/index.json

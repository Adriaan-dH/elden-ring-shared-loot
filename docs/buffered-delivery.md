# Buffered delivery candidate: 0.5.0-test.1

Implements the agreed design with no automatic award resends. No new commit, push or public release is part of this local implementation.

## State and protocol

Protocol version 2 retains the 208-byte envelope and dedicated Steam channel. Kinds 1/2 are hello/ack; kind 3 is the resolved award; kind 4 reports verified completion and kind 5 reports unresolved application. Reports reference the sender session generation and sequence through the target nonce/sequence fields. They never request an additional grant. 0.4.x packets are incompatible and rejected.

World readiness no longer resets nonces, sequence numbers or queues. Known addon peers remain recipients while heartbeat-stale or world-not-ready. The receiver accepts valid offers while unavailable and stores them in a bounded 512-bundle FIFO without a time expiry. The initial handshake still establishes membership/session identity; no history is sent to later participants.

The game frame leases the front delivery. If the inventory cannot be read before calling the native award function, the lease is deferred and checked again at most once per second. Once the native call happens, the attempt is terminal. Exact positive inventory/storage deltas produce completion. Zero, partial, excess, decreasing, or unavailable post-grant counts produce an unresolved report and never another attempt. Repeated item IDs in a bundle require the sum of their requested quantities, preventing one increment from satisfying multiple entries.

The sender tracks at most 1024 outstanding recipient deliveries for diagnostics. A completion response removes tracking and updates the result counters. A missing response after two minutes logs a single warning but does not resend anything. Steam send failures and receiver buffer overflow are explicit logged outcomes. All diagnostic queues are bounded; event-drop counters are visible.

Readiness changes, delivery transitions and verification records have monotonic timestamps. A random session generation plus sequence identifies a delivery across PCs. Peer labels are local anonymous numbers, not Steam IDs or IP addresses. Inventory verification records include requested quantities, before/after totals and whether the after snapshot was readable.

## Boundaries

Lobby changes, confirmed member removal, new peer generations and local character changes cancel scoped work and log cancellation. A zero/unreadable character identity during loading does not cause a reset. Buffers are not persisted to disk. Process exit/crash loses pending work. Inventory caps, unique-item duplicates and partial grants remain unresolved instead of being retried. Randomizer source filtering is unchanged.

## Game adapter evidence

The locked award disassembly loads GameDataMan at RVA 0x3d64fb8, main PlayerGameData at +8, equipment at +0x2b0 and storage at +0x8d0. The inventory getter at RVA 0x2488c0 is lea rax,[rcx+0x158]; ret, yielding PlayerGameData +0x408. Inventory accessors at +0x40/+0x48 and +0x50/+0x58, 24-byte entry stride and item/quantity fields are checked against local disassembly and read-only type documentation. Active key aliases are counted once, avoiding inactive multiplayer copies.

Death gating checks the ChrIns +0x190 module container, its data module, owner back-reference, in-image vtable, HP/max-HP at +0x138/+0x13c, and death bit 7 at ChrIns +0x1c5. A live positive-HP player must remain stable for 30 frames. Manager/player/inventory pointers are re-read; notification contexts are never retained. The actual award call remains serialized and guarded against rebroadcast.

Primary layout references used for investigation (no third-party implementation copied):

- https://github.com/vswarte/fromsoftware-rs/blob/main/crates/eldenring/src/cs/player_game_data.rs
- https://github.com/vswarte/fromsoftware-rs/blob/main/crates/eldenring/src/cs/chr_ins.rs
- https://github.com/vswarte/fromsoftware-rs/blob/main/crates/eldenring/src/cs/chr_ins/module/data.rs

These read-only integrations still require live confirmation against the installed guarded executable. Synthetic tests cannot prove death/loading recognition in a real Seamless session.

## Validation and next live test

Ten Linux tests pass, including new inventory probes and delivery buffering tests. Windows equivalents, guarded native hook/frame ABI checks and the fake Steam bridge are also checked after compilation. Tests include five-minute loading, expired heartbeats, duplicate offers, lost completion responses, failed sends, queue overflow, character/session cancellation, storage totals, inactive-key aliases, repeated item IDs and partial/unreadable grant outcomes. One native attempt is asserted when post-verification fails; failed preflight asserts zero calls.

Update both PCs to this candidate with the game closed. Launch the normal Mod Engine 2 profile. Collect while the recipient is alive, dead/spectating, in Roundtable/at a merchant, and during a warp/loading transition. Let one wait exceed 15 seconds. After revival/loading, verify the exact quantity arrived once. Also test storage/capacity, quitting/rejoining and deliberately collecting another world copy. Keep logs from both PCs and correlate generation + delivery. Look for completed/completion_received or an explicit unresolved reason. Do not publish the candidate before this live validation.

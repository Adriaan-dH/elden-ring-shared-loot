# Networking investigation — 2026-11-04

Update: the proposed replacement is implemented experimentally in build 0.4.0. See `steam-test.md` and the current section of `development-status.md`. Native mocks validate discovery callbacks and messaging calls; real Seamless coexistence and game-frame delivery still need a live run.

## Failed LAN test

The host's build 0.3.0 log reported the acquisition hook as `MH_OK`, but the TCP bridge stayed at state 1 (waiting/connecting). It never recorded authentication or readiness, `rowa_sent`, or a received message. It did record the user's local Rowa Fruit pickup. Therefore the observed failure occurred before cross-PC item delivery. The host log alone cannot distinguish a client setup/loading problem, wrong address/token, or firewall/network reachability issue. It does not demonstrate that the item-grant routine failed.

Joining through Seamless did not automatically connect the separate TCP prototype. The user now requires identical, role-independent addon setup, dynamic membership, all participants able to send, and no configured/published IP addresses. The IP-based LAN prototype is not the intended release architecture.

## Evidence about Seamless

- The author's description documents Steam matchmaking/newer peer networking and exclusion from FromSoftware matchmaking: https://www.nexusmods.com/eldenring/mods/510
- The public repository is a release mirror, not an extension SDK: https://github.com/yuiamoroll/EldenRingSeamlessCoopRelease
- Read-only inspection of the installed `ersc.dll` found a `steam_api64.dll` import, `SteamAPI_GetHSteamPipe`, and strings `SteamNetworking006`, `SteamNetworkingUtils003`, and `SteamNetworkingMessages002`. These support investigating Steam Messaging as the integration point; strings/imports do not prove which paths/channels are active in the present session.
- Public game-structure research describes `CSSessionManager` session states and player entries containing Steam IDs, and network-session identity accessors. Compatibility of those structures with the installed executable and Seamless's modifications is not yet validated:
  - https://github.com/vswarte/fromsoftware-rs/blob/main/crates/eldenring/src/cs/session_manager.rs
  - https://github.com/vswarte/fromsoftware-rs/blob/main/crates/eldenring/src/cs/network_session.rs

## Proposed replacement

Use one addon build/configuration for every player. Discover the actual current Seamless session's peers, establish an addon-version handshake, and send each eligible acquisition directly to the other validated peers using Steam identities. Anyone can collect and broadcast; the addon's networking does not need a permanent host address or manual host/client selection. The game can still have its normal Seamless host. If Seamless ends the party when that host leaves, the addon must also clear state; do not assume Steam lobby-owner migration makes Seamless world migration work.

Valve exposes lobby member enumeration and owner/member-change callbacks. However, those methods require knowing the lobby ID, and a lobby member is not automatically a fully loaded in-world participant. There is no generic `GetCurrentLobby()` accessor in this interface. We must obtain/validate the right lobby or peer set through game/session state or observing Steam lobby lifecycle callbacks. Searching arbitrary public lobbies or connecting to all Steam friends would be incorrect.

Use an independent `ISteamNetworkingMessages` channel, or a dedicated `ISteamNetworkingSockets` virtual port if message-channel coexistence proves problematic. Validate channel/port availability against Seamless's actual use before choosing it. Do not inject new packet types into Seamless's undocumented game protocol, drain its queues, replace its callbacks, or close its shared peer connections. Only read our own channel and close our own channel/connection. Avoid independently initializing/shutting down the game's Steam client or taking over its callback pump.

Steam Messaging can address peers by Steam identity and offers reliable ordered delivery within a channel. It implicitly establishes connections, allowing symmetric peer use without fixed host/client roles. It shares underlying connections across channels, so coexistence is a real engineering requirement, not a guarantee. If IP privacy is a goal beyond removing configuration addresses, verify relay-only routing; merely selecting a Steam API is not proof that all traffic uses SDR. Valve's relay transport authenticates/encrypts traffic and avoids exposing peer IP addresses when relay routing is used.

Each participant must run the addon. Validate sender membership in the current game party, protocol compatibility, message sizes/types, current-session identity, and monotonically identified pickup events. Do not accept gifts from arbitrary Steam IDs. Discard queues and peer state on disconnect, world/session change, or character change. Rejoining does not replay old pickups in v1.

## Next diagnostic milestone

Build a read-only session/Steam integration probe for the next restart: observe party join/leave and list validated peers with identities redacted in ordinary logs. Verify discovery when this PC hosts and when it joins another host. Then exchange an addon ping/ack over an isolated channel before enabling items. A live process currently running the old DLL cannot acquire these new callbacks without a restart; no running game was patched during this investigation.

Two separate problems remain after dynamic networking: reliable provenance filtering (NPC/scarab rewards are already shared; storage/shop operations stay excluded), and periodic game-thread delivery so a recipient need not collect another item to service a queue. Steam transport does not solve either problem by itself.

## Publication hygiene

The earlier personal LAN address was removed from source documentation. The LAN packaging script takes an address as a local argument, not as a source-code constant. Generated tokens/configurations and test/research logs remain in ignored local workspace files or external user-delivery outputs. ZIPs, local INI overrides, and local setup notes are ignored. No commit or push has occurred. Future release configuration should contain no IP, personal Steam ID, or shared password/token.

## Primary API references

- Lobby enumeration and lifecycle: https://partner.steamgames.com/doc/api/ISteamMatchmaking
- Identity-addressed messaging/channels: https://partner.steamgames.com/doc/api/ISteamNetworkingMessages
- Symmetric connections/virtual ports: https://partner.steamgames.com/doc/api/ISteamNetworkingSockets
- Relay routing/privacy: https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay

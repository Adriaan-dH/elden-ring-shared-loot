#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include "steam_bridge.h"
#include "shared_loot.h"
#include <algorithm>
namespace shared_loot {
namespace {
template <class T> T symbol(HMODULE module, const char* name) {
    auto address = GetProcAddress(module, name);
    T result = nullptr;
    static_assert(sizeof(result) == sizeof(address));
    std::memcpy(&result, &address, sizeof(result));
    return result;
}
} // namespace
void SteamBridge::entered(void* owner, void* parameter) {
    auto* self = static_cast<SteamBridge*>(owner);
    auto* p = static_cast<steam::LobbyEnter*>(parameter);
    ++self->callback_count_;
    if (p && p->response == 1)
        self->lobby_ = p->lobby;
}
void SteamBridge::created(void* owner, void* parameter) {
    auto* self = static_cast<SteamBridge*>(owner);
    auto* p = static_cast<steam::LobbyCreated*>(parameter);
    ++self->callback_count_;
    if (p && p->result == 1)
        self->lobby_ = p->lobby;
}
void SteamBridge::changed(void* owner, void*) {
    ++static_cast<SteamBridge*>(owner)->callback_count_;
}
// Reuse the game-owned Steam interfaces. Do not initialize/shut down Steam
// or pump its callback loop here; the game already owns that lifecycle.
bool SteamBridge::start() {
    if (available_)
        return true;
    auto module = GetModuleHandleW(L"steam_api64.dll");
    if (!module)
        return false;
    auto matchmaking = symbol<void* (*)()>(module, "SteamAPI_SteamMatchmaking_v009");
    auto messaging = symbol<void* (*)()>(module, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002");
    auto user = symbol<void* (*)()>(module, "SteamAPI_SteamUser_v021");
    auto user_id = symbol<std::uint64_t (*)(void*)>(module, "SteamAPI_ISteamUser_GetSteamID");
    auto friends = symbol<void* (*)()>(module, "SteamAPI_SteamFriends_v017");
    register_callback_ = symbol<decltype(register_callback_)>(module, "SteamAPI_RegisterCallback");
    member_count_ =
        symbol<decltype(member_count_)>(module, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers");
    member_at_ =
        symbol<decltype(member_at_)>(module, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex");
    owner_ = symbol<decltype(owner_)>(module, "SteamAPI_ISteamMatchmaking_GetLobbyOwner");
    send_ = symbol<decltype(send_)>(module, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser");
    receive_ = symbol<decltype(receive_)>(
        module, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel");
    accept_ = symbol<decltype(accept_)>(module,
                                        "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser");
    friend_count_ =
        symbol<decltype(friend_count_)>(module, "SteamAPI_ISteamFriends_GetFriendCount");
    friend_at_ = symbol<decltype(friend_at_)>(module, "SteamAPI_ISteamFriends_GetFriendByIndex");
    friend_game_ =
        symbol<decltype(friend_game_)>(module, "SteamAPI_ISteamFriends_GetFriendGamePlayed");
    if (!matchmaking || !messaging || !user || !user_id || !register_callback_ || !member_count_ ||
        !member_at_ || !owner_ || !send_ || !receive_ || !accept_)
        return false;
    matchmaking_ = matchmaking();
    messaging_ = messaging();
    void* user_interface = user();
    if (!matchmaking_ || !messaging_ || !user_interface)
        return false;
    self_ = user_id(user_interface);
    if (!self_)
        return false;
    friends_ = friends ? friends() : nullptr;
    std::uint64_t nonce = 0;
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&nonce), sizeof(nonce),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
        !nonce)
        return false;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        router_.initialize(self_, nonce);
        router_.set_shop_sharing(share_shops_);
    }
    entered_.owner = this;
    entered_.handler = &entered;
    entered_.bytes = sizeof(steam::LobbyEnter);
    created_.owner = this;
    created_.handler = &created;
    created_.bytes = sizeof(steam::LobbyCreated);
    changed_.owner = this;
    changed_.handler = &changed;
    changed_.bytes = sizeof(steam::LobbyChatUpdate);
    register_callback_(&entered_, 504);
    register_callback_(&created_, 513);
    register_callback_(&changed_, 506);
    available_ = true;
    return true;
}
// Discovering a lobby through a callback or friend is not enough: require
// Steam to report the local account as a member before accepting its peers.
bool SteamBridge::verify_lobby(std::uint64_t candidate, std::vector<std::uint64_t>& ids) {
    if (!candidate || !owner_(matchmaking_, candidate))
        return false;
    int count = member_count_(matchmaking_, candidate);
    if (count < 1 || count > 8)
        return false;
    ids.clear();
    for (int i = 0; i < count; ++i) {
        auto id = member_at_(matchmaking_, candidate, i);
        if (!id)
            return false;
        ids.push_back(id);
    }
    return std::find(ids.begin(), ids.end(), self_) != ids.end();
}
void SteamBridge::refresh_members(std::uint64_t now) {
    if (now - last_members_ < 1000)
        return;
    last_members_ = now;
    auto candidate = lobby_.load();
    std::vector<std::uint64_t> ids;
    if (!verify_lobby(candidate, ids)) {
        candidate = 0;
        ids.clear();
        // Read-only fallback if the addon missed an early lobby callback.
        // A friend's lobby is accepted only if Steam confirms LOCAL membership.
        if (friends_ && friend_game_) {
            steam::FriendGame game{};
            if (friend_game_(friends_, self_, &game) && (game.game & 0xffffff) == 1245620 &&
                verify_lobby(game.lobby, ids))
                candidate = game.lobby;
            if (!candidate && friend_count_ && friend_at_) {
                int count = std::min(friend_count_(friends_, 4), 512);
                for (int i = 0; i < count && !candidate; ++i) {
                    auto id = friend_at_(friends_, i, 4);
                    game = {};
                    if (friend_game_(friends_, id, &game) && (game.game & 0xffffff) == 1245620 &&
                        verify_lobby(game.lobby, ids))
                        candidate = game.lobby;
                }
            }
        }
        lobby_ = candidate;
        if (!candidate)
            ids.clear();
    }
    {
        std::lock_guard<std::mutex> guard(mutex_);
        router_.members(candidate, ids);
    }
    // Only known current lobby members; do not reject/close other Steam sessions.
    for (auto id : ids)
        if (id != self_) {
            auto identity = steam::Identity::user(id);
            accept_(messaging_, &identity);
        }
}
// The worker services only our dedicated message channel. It does not drain
// Seamless traffic or close networking sessions owned by other mods.
void SteamBridge::tick(std::uint64_t now) {
    if (!available_)
        return;
    refresh_members(now);
    {
        std::lock_guard<std::mutex> guard(mutex_);
        router_.tick(now);
    }
    for (unsigned i = 0; i < 128; ++i) {
        RoutedMessage message{};
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!router_.take_send(message))
                break;
        }
        auto identity = steam::Identity::user(message.peer);
        // Reliable, AutoRestartBrokenSession. Our own channel only.
        int result = send_(messaging_, &identity, message.wire.bytes.data(), shared_wire_size,
                           8 | 32, steam_channel);
        {
            std::lock_guard<std::mutex> guard(mutex_);
            router_.send_result(message, result, now);
        }
        if (result != 1)
            ++errors_;
        if (message.wire.bytes[7] == 1)
            ++hello_frames_;
    }
    steam::Message* messages[32]{};
    int count = receive_(messaging_, steam_channel, messages, 32);
    for (int i = 0; i < count && i < 32; ++i) {
        auto* message = messages[i];
        if (!message)
            continue;
        if (message->channel == steam_channel && message->size >= 0 && message->size <= 4096) {
            auto id = message->peer.user();
            std::lock_guard<std::mutex> guard(mutex_);
            router_.receive(id, message->data, static_cast<std::size_t>(message->size), now);
        }
        if (message->release)
            message->release(message);
    }
}
// The frame task and award hook call these while the worker polls Steam.
// Serialize access to the portable router without exposing game pointers to it.
void SteamBridge::set_shop_sharing(bool enabled) {
    std::lock_guard<std::mutex> guard(mutex_);
    share_shops_ = enabled;
    router_.set_shop_sharing(enabled);
}
void SteamBridge::set_world(bool ready) {
    std::lock_guard<std::mutex> guard(mutex_);
    world_ = ready;
    router_.world(ready);
}
void SteamBridge::set_character(std::uint64_t identity) {
    std::lock_guard<std::mutex> guard(mutex_);
    router_.character(identity);
}
bool SteamBridge::defer(const Delivery& d, std::uint64_t now) {
    std::lock_guard<std::mutex> guard(mutex_);
    return router_.defer_delivery(d, now);
}
bool SteamBridge::finish(const Delivery& d, DeliveryReason reason, std::uint64_t now) {
    std::lock_guard<std::mutex> guard(mutex_);
    return router_.finish_delivery(d, reason, now);
}
bool SteamBridge::take_event(DeliveryEvent& e) {
    std::lock_guard<std::mutex> guard(mutex_);
    return router_.take_event(e);
}
unsigned SteamBridge::capture(const AwardPacket& packet, std::uint64_t now, bool shop_purchase) {
    std::lock_guard<std::mutex> guard(mutex_);
    return router_.capture(packet, now, shop_purchase);
}
bool SteamBridge::take(Delivery& out, std::uint64_t now) {
    std::lock_guard<std::mutex> guard(mutex_);
    return router_.take_delivery(out, now);
}
SteamStatus SteamBridge::status() {
    std::lock_guard<std::mutex> guard(mutex_);
    return {available_, lobby_.load() != 0,     world_,       router_.stats(),
            errors_,    callback_count_.load(), hello_frames_};
}
} // namespace shared_loot
// Test entry point: refuse to run unless the isolated fake Steam library
// exposes its marker. This exercises ABI integration without launching the game.
extern "C" SL_API int SL_SteamAdapterMockTest() {
    using namespace shared_loot;
    auto module = GetModuleHandleW(L"steam_api64.dll");
    if (!module)
        return -1;
    auto marker = symbol<int (*)()>(module, "SharedLootMockMarker");
    if (!marker || marker() != 404)
        return -2;
    auto reset = symbol<void (*)()>(module, "MockReset");
    auto enter = symbol<void (*)()>(module, "MockEnter");
    auto time = symbol<void (*)(std::uint64_t)>(module, "MockTime");
    auto take = symbol<int (*)(AwardPacket*)>(module, "MockTake");
    auto inject = symbol<int (*)(const AwardPacket*)>(module, "MockInject");
    if (!reset || !enter || !time || !take || !inject)
        return -3;
    reset();
    SteamBridge bridge;
    if (!bridge.start())
        return -4;
    bridge.set_world(true);
    enter();
    for (unsigned i = 0; i < 10; ++i) {
        time(2000 + i * 1000);
        bridge.tick(2000 + i * 1000);
    }
    if (bridge.status().router.ready != 1)
        return -5;
    AwardPacket packet{};
    packet.count = 3;
    packet.entries[0] = {0x400050f0, 3, 0xffffffff, 0xffffffff};
    packet.entries[1] = {0x002e3bf0, 1, 0xffffffff, 10002};
    packet.entries[2] = {0x1019f104, 1, 0xffffffff, 0xffffffff};
    if (bridge.capture(packet, 12000) != 1)
        return -6;
    time(12000);
    bridge.tick(12000);
    AwardPacket received{};
    if (!take(&received) || std::memcmp(&packet, &received, sizeof(packet)))
        return -7;
    if (inject(&packet) != 1)
        return -8;
    bridge.tick(12001);
    Delivery delivery{};
    if (!bridge.take(delivery, 12001) || std::memcmp(&packet, &delivery.packet, sizeof(packet)))
        return -9;
    if (!bridge.finish(delivery, DeliveryReason::Verified, 12001))
        return -11;
    if (bridge.take(delivery, 12001) || bridge.status().send_errors)
        return -10;
    reset();
    return 0;
}

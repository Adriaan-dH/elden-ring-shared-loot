#pragma once
#include "session_router.h"
#include "steam_abi.h"
#include <atomic>
#include <mutex>
namespace shared_loot {
struct SteamStatus {
    bool available = false, lobby = false, world = false;
    RouterStats router{};
    std::uint64_t send_errors = 0, callbacks = 0, hellos = 0;
};
// Windows adapter between Steam flat exports and SessionRouter. Raw Steam IDs
// are routing identities only; the addon does not need configured IPs or roles.
class SteamBridge {
  public:
    bool start();
    void tick(std::uint64_t now);
    void set_world(bool ready);
    unsigned capture(const AwardPacket&, std::uint64_t now);
    bool take(Delivery&, std::uint64_t now);
    SteamStatus status();

  private:
    void refresh_members(std::uint64_t now);
    bool verify_lobby(std::uint64_t candidate, std::vector<std::uint64_t>& ids);
    static void entered(void*, void*);
    static void created(void*, void*);
    static void changed(void*, void*);
    std::mutex mutex_;
    SessionRouter router_;
    std::atomic<std::uint64_t> lobby_{0}, callback_count_{0};
    bool available_ = false, world_ = false;
    std::uint64_t self_ = 0, last_members_ = 0, errors_ = 0, hello_frames_ = 0;
    void* matchmaking_ = nullptr;
    void* messaging_ = nullptr;
    void* friends_ = nullptr;
    steam::Callback entered_, created_, changed_;
    void (*register_callback_)(steam::CallbackBase*, int) = nullptr;
    int (*member_count_)(void*, std::uint64_t) = nullptr;
    std::uint64_t (*member_at_)(void*, std::uint64_t, int) = nullptr;
    std::uint64_t (*owner_)(void*, std::uint64_t) = nullptr;
    int (*send_)(void*, const steam::Identity*, const void*, std::uint32_t, int, int) = nullptr;
    int (*receive_)(void*, int, steam::Message**, int) = nullptr;
    bool (*accept_)(void*, const steam::Identity*) = nullptr;
    int (*friend_count_)(void*, int) = nullptr;
    std::uint64_t (*friend_at_)(void*, int, int) = nullptr;
    bool (*friend_game_)(void*, std::uint64_t, steam::FriendGame*) = nullptr;
};
} // namespace shared_loot

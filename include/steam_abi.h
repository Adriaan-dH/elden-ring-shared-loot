#pragma once
// Narrow ABI declarations for dynamically resolved Steam exports. Layouts are
// documented by Valve's public steamnetworkingtypes.h and matchmaking callbacks.
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace shared_loot::steam {
#pragma pack(push, 1)
struct Identity {
    std::int32_t type = 0, size = 0;
    unsigned char data[128]{};
    static Identity user(std::uint64_t id) {
        Identity value{};
        value.type = 16;
        value.size = 8;
        std::memcpy(value.data, &id, 8);
        return value;
    }
    std::uint64_t user() const {
        std::uint64_t id = 0;
        if (type == 16 && size == 8)
            std::memcpy(&id, data, 8);
        return id;
    }
};
#pragma pack(pop)
struct Message {
    void* data;
    std::int32_t size;
    std::uint32_t connection;
    Identity peer;
    std::int64_t connection_user, received_at, number;
    void (*free_data)(Message*);
    void (*release)(Message*);
    std::int32_t channel, flags;
    std::int64_t user_data;
    std::uint16_t lane, padding;
};
struct LobbyEnter {
    std::uint64_t lobby;
    std::uint32_t permissions;
    bool locked;
    std::uint8_t padding[3];
    std::uint32_t response;
};
struct LobbyCreated {
    std::int32_t result;
    std::uint32_t padding;
    std::uint64_t lobby;
};
struct LobbyChatUpdate {
    std::uint64_t lobby, changed, actor;
    std::uint32_t state;
};
struct FriendGame {
    std::uint64_t game;
    std::uint32_t ip;
    std::uint16_t port, query_port;
    std::uint64_t lobby;
};
static_assert(sizeof(Identity) == 136 && alignof(Identity) == 1);
static_assert(offsetof(Message, peer) == 16 && offsetof(Message, release) == 184 &&
              offsetof(Message, channel) == 192);
static_assert(sizeof(LobbyEnter) == 24 && offsetof(LobbyEnter, response) == 16);
static_assert(sizeof(LobbyCreated) == 16 && sizeof(LobbyChatUpdate) == 32 &&
              sizeof(FriendGame) == 24);
// Steam callback objects have three dispatch slots, followed by flags and ID.
class CallbackBase {
  public:
    virtual void run(void*) = 0;
    virtual void run_call(void*, bool, std::uint64_t) = 0;
    virtual int size() = 0;
    std::uint8_t flags = 0;
    std::int32_t id = 0;
};
static_assert(sizeof(CallbackBase) == 16);
class Callback final : public CallbackBase {
  public:
    void* owner = nullptr;
    void (*handler)(void*, void*) = nullptr;
    int bytes = 0;
    void run(void* p) override {
        if (handler)
            handler(owner, p);
    }
    void run_call(void* p, bool failure, std::uint64_t) override {
        if (!failure)
            run(p);
    }
    int size() override {
        return bytes;
    }
};
} // namespace shared_loot::steam

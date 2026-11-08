// Test-only DLL. Never include this file/binary in a game installation/package.
#include "steam_abi.h"
#include "session_router.h"
#include <vector>
#include <deque>
#include <cstring>
using namespace shared_loot;
using namespace shared_loot::steam;
#define API extern "C" __declspec(dllexport)
namespace {
std::vector<CallbackBase*> callbacks;
SessionRouter remote;
std::deque<RoutedMessage> response;
std::uint64_t clock_ms = 2000;
int interface_object = 1;
void collect() {
    RoutedMessage message{};
    while (remote.take_send(message))
        response.push_back(message);
}
void release_message(Message* message) {
    delete[] static_cast<unsigned char*>(message->data);
    delete message;
}
} // namespace
API int SharedLootMockMarker() {
    return 404;
}
API void MockReset() {
    callbacks.clear();
    response.clear();
    remote.initialize(202, 20001);
    remote.members(456, {101, 202});
    remote.world(true);
}
API void MockEnter() {
    LobbyEnter event{456, 0, false, {0, 0, 0}, 1};
    for (auto* cb : callbacks)
        if (cb->id == 504)
            cb->run(&event);
}
API void MockTime(std::uint64_t value) {
    clock_ms = value;
    remote.tick(value);
    collect();
}
API int MockTake(AwardPacket* out) {
    Delivery delivery{};
    if (!remote.take_delivery(delivery, clock_ms))
        return 0;
    remote.finish_delivery(delivery, DeliveryReason::Verified, clock_ms);
    *out = delivery.packet;
    return 1;
}
API int MockInject(const AwardPacket* packet) {
    unsigned count = remote.capture(*packet, clock_ms);
    collect();
    return static_cast<int>(count);
}
API void* SteamAPI_SteamMatchmaking_v009() {
    return &interface_object;
}
API void* SteamAPI_SteamNetworkingMessages_SteamAPI_v002() {
    return &interface_object;
}
API void* SteamAPI_SteamUser_v021() {
    return &interface_object;
}
API std::uint64_t SteamAPI_ISteamUser_GetSteamID(void*) {
    return 101;
}
API void SteamAPI_RegisterCallback(CallbackBase* cb, int id) {
    cb->id = id;
    callbacks.push_back(cb);
}
API int SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(void*, std::uint64_t lobby) {
    return lobby == 456 ? 2 : 0;
}
API std::uint64_t SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(void*, std::uint64_t lobby,
                                                                   int index) {
    return lobby == 456 ? (index == 0 ? 101 : index == 1 ? 202 : 0) : 0;
}
API std::uint64_t SteamAPI_ISteamMatchmaking_GetLobbyOwner(void*, std::uint64_t lobby) {
    return lobby == 456 ? 101 : 0;
}
API bool SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser(void*, const Identity* identity) {
    return identity->user() == 202;
}
API int SteamAPI_ISteamNetworkingMessages_SendMessageToUser(void*, const Identity* identity,
                                                            const void* data, std::uint32_t size,
                                                            int flags, int channel) {
    if (identity->user() != 202 || channel != steam_channel || (flags & 8) == 0)
        return 8;
    remote.receive(101, data, size, clock_ms);
    collect();
    return 1;
}
API int SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel(void*, int channel,
                                                                   Message** out, int capacity) {
    if (channel != steam_channel)
        return 0;
    int count = 0;
    while (count < capacity && !response.empty()) {
        auto message = response.front();
        response.pop_front();
        auto* result = new Message{};
        result->data = new unsigned char[shared_wire_size];
        std::memcpy(result->data, message.wire.bytes.data(), shared_wire_size);
        result->size = shared_wire_size;
        result->peer = Identity::user(202);
        result->channel = channel;
        result->release = &release_message;
        out[count++] = result;
    }
    return count;
}

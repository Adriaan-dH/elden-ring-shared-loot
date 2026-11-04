#pragma once
#include "award_packet.h"
#include <array>
#include <cstdint>
#include <deque>
#include <vector>
namespace shared_loot {
constexpr int steam_channel = 0x534c3401;
constexpr std::size_t shared_wire_size = 208;
struct SharedWire {
    std::array<unsigned char, shared_wire_size> bytes{};
};
struct RoutedMessage {
    std::uint64_t peer = 0;
    SharedWire wire{};
};
struct Delivery {
    std::uint64_t peer = 0, nonce = 0, received_at = 0;
    AwardPacket packet{};
};
struct RouterStats {
    unsigned members = 0, ready = 0, pending = 0;
    std::uint64_t sent = 0, received = 0, rejected = 0;
};
bool valid_shared_packet(const AwardPacket& packet);
// Platform-independent session and packet state. Callers provide synchronization
// and monotonic millisecond timestamps; this class never touches Steam or the game.
class SessionRouter {
  public:
    void initialize(std::uint64_t self, std::uint64_t nonce);
    void members(std::uint64_t lobby, const std::vector<std::uint64_t>& ids);
    void world(bool ready);
    void tick(std::uint64_t now);
    unsigned capture(const AwardPacket&, std::uint64_t now);
    bool receive(std::uint64_t peer, const void* bytes, std::size_t length, std::uint64_t now);
    bool take_send(RoutedMessage&);
    bool take_delivery(Delivery&, std::uint64_t now);
    RouterStats stats() const;
    bool member(std::uint64_t id) const;

  private:
    struct Peer {
        std::uint64_t id = 0, nonce = 0, last_sequence = 0, last_seen = 0;
        bool ack = false, world = false;
    };
    Peer* find(std::uint64_t id);
    void reset();
    bool enqueue(Peer&, unsigned kind, std::uint64_t sequence, const AwardPacket*);
    std::uint64_t self_ = 0, nonce_ = 0, lobby_ = 0, sequence_ = 1, last_hello_ = 0;
    bool world_ = false;
    std::vector<Peer> peers_;
    std::deque<RoutedMessage> outgoing_;
    std::deque<Delivery> incoming_;
    RouterStats counters_{};
};
} // namespace shared_loot

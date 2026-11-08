#pragma once
#include "award_packet.h"
#include <array>
#include <cstdint>
#include <deque>
#include <vector>
namespace shared_loot {
constexpr int steam_channel = 0x534c3401;
constexpr std::size_t shared_wire_size = 208;
// Version 3 adds shop provenance and receiver opt-out. Update every PC together.
constexpr unsigned shared_protocol_version = 3;
constexpr std::size_t delivery_capacity = 512;
enum class DeliveryReason : unsigned char {
    None,
    InventoryUnavailable,
    Verified,
    NoChange,
    PartialOrUnexpected,
    VerificationUnavailable,
    BufferFull,
    TrackingFull,
    SessionEnded,
    PeerChanged,
    CharacterChanged,
    SteamSendFailed,
    MissingCompletion,
    Malformed,
    Duplicate,
    ShopSharingDisabled
};
enum class DeliveryAction {
    Offered,
    Buffered,
    Applying,
    Deferred,
    Completed,
    Unresolved,
    CompletionReceived,
    FailureReceived,
    Cancelled,
    Rejected,
    SendFailed,
    CompletionMissing
};
const char* delivery_action_name(DeliveryAction);
const char* delivery_reason_name(DeliveryReason);
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
    std::uint64_t sequence = 0;
    bool shop_purchase = false;
};
struct DeliveryEvent {
    DeliveryAction action{};
    DeliveryReason reason{};
    unsigned peer_label = 0;
    std::uint64_t generation = 0, sequence = 0, time = 0, age = 0;
    int detail = 0;
    AwardPacket packet{};
    bool shop_purchase = false;
};
struct RouterStats {
    unsigned members = 0, ready = 0, pending = 0, awaiting_completion = 0;
    std::uint64_t sent = 0, received = 0, rejected = 0, completed = 0, unresolved = 0,
                  confirmed_by_peer = 0, failed_by_peer = 0, event_drops = 0;
};
bool valid_shared_packet(const AwardPacket& packet);
// Caller serializes access. Session identity survives death/loading; readiness
// gates only game-thread application. There is no award retry/resend path.
class SessionRouter {
  public:
    void initialize(std::uint64_t self, std::uint64_t nonce);
    void members(std::uint64_t lobby, const std::vector<std::uint64_t>& ids);
    void character(std::uint64_t identity);
    void world(bool ready);
    void set_shop_sharing(bool enabled);
    void tick(std::uint64_t now);
    unsigned capture(const AwardPacket&, std::uint64_t now, bool shop_purchase = false);
    bool receive(std::uint64_t peer, const void*, std::size_t, std::uint64_t now);
    bool take_send(RoutedMessage&);
    void send_result(const RoutedMessage&, int result, std::uint64_t now);
    // Lease the front award without removing it. Defer is permitted only before
    // a native grant attempt; finish is terminal even if verification fails.
    bool take_delivery(Delivery&, std::uint64_t now);
    bool defer_delivery(const Delivery&, std::uint64_t now);
    bool finish_delivery(const Delivery&, DeliveryReason, std::uint64_t now);
    bool take_event(DeliveryEvent&);
    RouterStats stats() const;
    bool member(std::uint64_t id) const;

  private:
    struct Peer {
        std::uint64_t id = 0, nonce = 0, last_sequence = 0, last_seen = 0;
        bool ack = false, world = false;
        unsigned label = 0;
    };
    struct Pending {
        Delivery value;
        bool applying = false, deferred_logged = false;
    };
    struct Outstanding {
        std::uint64_t peer, peer_nonce, sequence, offered_at;
        AwardPacket packet;
        bool warned = false;
        bool shop_purchase = false;
    };
    Peer* find(std::uint64_t);
    void reset(DeliveryReason);
    void prune(std::uint64_t peer, DeliveryReason);
    bool enqueue(Peer&, unsigned kind, std::uint64_t, const AwardPacket*,
                 DeliveryReason reason = DeliveryReason::None);
    void event(DeliveryAction, DeliveryReason, const Delivery&, int detail = 0);
    std::uint64_t self_ = 0, nonce_ = 0, lobby_ = 0, sequence_ = 1, last_hello_ = 0, character_ = 0,
                  now_ = 0;
    unsigned next_label_ = 1;
    bool world_ = false, share_shops_ = true;
    std::vector<Peer> peers_;
    std::deque<RoutedMessage> outgoing_;
    std::deque<Pending> incoming_;
    std::deque<Outstanding> outstanding_;
    std::deque<DeliveryEvent> events_;
    RouterStats counters_{};
};
} // namespace shared_loot

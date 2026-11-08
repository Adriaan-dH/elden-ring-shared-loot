#include "session_router.h"
#include <algorithm>
#include <cstring>
namespace shared_loot {
namespace {
// Encode explicitly in network byte order instead of copying C++ structs.
// This keeps padding and host endianness out of the wire protocol.
void put32(unsigned char* p, std::uint32_t v) {
    for (int i = 3; i >= 0; --i) {
        p[i] = static_cast<unsigned char>(v);
        v >>= 8;
    }
}
void put64(unsigned char* p, std::uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        p[i] = static_cast<unsigned char>(v);
        v >>= 8;
    }
}
std::uint32_t get32(const unsigned char* p) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v = (v << 8) | p[i];
    return v;
}
std::uint64_t get64(const unsigned char* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | p[i];
    return v;
}
} // namespace
// Reject the whole award bundle if any entry is malformed. Category bits
// distinguish equipment, goods and gems; quantities have conservative bounds.
bool valid_shared_packet(const AwardPacket& p) {
    if (!p.count || p.count > 10)
        return false;
    for (unsigned i = 0; i < p.count; ++i) {
        auto& e = p.entries[i];
        unsigned category = e.item >> 28;
        if (!(e.item & 0x0fffffff) ||
            (category != 0 && category != 1 && category != 2 && category != 4 && category != 8) ||
            !e.quantity || e.quantity > (category == 4 ? 999u : 99u))
            return false;
    }
    return true;
}
const char* delivery_action_name(DeliveryAction a) {
    switch (a) {
    case DeliveryAction::Offered:
        return "offered";
    case DeliveryAction::Buffered:
        return "buffered";
    case DeliveryAction::Applying:
        return "applying";
    case DeliveryAction::Deferred:
        return "deferred";
    case DeliveryAction::Completed:
        return "completed";
    case DeliveryAction::Unresolved:
        return "unresolved";
    case DeliveryAction::CompletionReceived:
        return "completion_received";
    case DeliveryAction::FailureReceived:
        return "failure_received";
    case DeliveryAction::Cancelled:
        return "cancelled";
    case DeliveryAction::Rejected:
        return "rejected";
    case DeliveryAction::SendFailed:
        return "send_failed";
    case DeliveryAction::CompletionMissing:
        return "completion_missing";
    }
    return "unknown";
}
const char* delivery_reason_name(DeliveryReason r) {
    switch (r) {
    case DeliveryReason::None:
        return "none";
    case DeliveryReason::InventoryUnavailable:
        return "inventory_unavailable";
    case DeliveryReason::Verified:
        return "inventory_delta_verified";
    case DeliveryReason::NoChange:
        return "no_inventory_change";
    case DeliveryReason::PartialOrUnexpected:
        return "partial_or_unexpected_delta";
    case DeliveryReason::VerificationUnavailable:
        return "post_grant_verification_unavailable";
    case DeliveryReason::BufferFull:
        return "receiver_buffer_full";
    case DeliveryReason::TrackingFull:
        return "sender_tracking_full";
    case DeliveryReason::SessionEnded:
        return "session_ended";
    case DeliveryReason::PeerChanged:
        return "peer_left_or_changed_session";
    case DeliveryReason::CharacterChanged:
        return "character_changed";
    case DeliveryReason::SteamSendFailed:
        return "steam_send_failed_no_retry";
    case DeliveryReason::MissingCompletion:
        return "completion_not_observed_no_retry";
    case DeliveryReason::Malformed:
        return "invalid_or_incompatible_message";
    case DeliveryReason::Duplicate:
        return "duplicate_or_old_delivery";
    case DeliveryReason::ShopSharingDisabled:
        return "shop_sharing_disabled_on_receiver";
    }
    return "unknown";
}
void SessionRouter::event(DeliveryAction action, DeliveryReason reason, const Delivery& d,
                          int detail) {
    if (events_.size() >= 2048) {
        ++counters_.event_drops;
        return;
    }
    auto* p = find(d.peer);
    events_.push_back({action, reason, p ? p->label : 0, d.nonce, d.sequence, now_,
                       now_ >= d.received_at ? now_ - d.received_at : 0, detail, d.packet,
                       d.shop_purchase});
}
void SessionRouter::initialize(std::uint64_t self, std::uint64_t nonce) {
    *this = SessionRouter{};
    self_ = self;
    nonce_ = nonce ? nonce : 1;
}
void SessionRouter::prune(std::uint64_t id, DeliveryReason reason) {
    for (const auto& p : incoming_)
        if (p.value.peer == id)
            event(DeliveryAction::Cancelled, reason, p.value);
    for (const auto& p : outstanding_)
        if (p.peer == id)
            event(DeliveryAction::Cancelled, reason,
                  {p.peer, nonce_, p.offered_at, p.packet, p.sequence, p.shop_purchase});
    incoming_.erase(std::remove_if(incoming_.begin(), incoming_.end(),
                                   [&](const Pending& p) { return p.value.peer == id; }),
                    incoming_.end());
    outstanding_.erase(std::remove_if(outstanding_.begin(), outstanding_.end(),
                                      [&](const Outstanding& p) { return p.peer == id; }),
                       outstanding_.end());
    outgoing_.erase(std::remove_if(outgoing_.begin(), outgoing_.end(),
                                   [&](const RoutedMessage& p) { return p.peer == id; }),
                    outgoing_.end());
}
void SessionRouter::reset(DeliveryReason reason) {
    for (const auto& p : peers_)
        prune(p.id, reason);
    outgoing_.clear();
    incoming_.clear();
    outstanding_.clear();
    sequence_ = 1;
    last_hello_ = 0;
    for (auto& p : peers_) {
        p.nonce = 0;
        p.last_sequence = 0;
        p.last_seen = 0;
        p.ack = false;
        p.world = false;
    }
}
SessionRouter::Peer* SessionRouter::find(std::uint64_t id) {
    for (auto& p : peers_)
        if (p.id == id)
            return &p;
    return nullptr;
}
bool SessionRouter::member(std::uint64_t id) const {
    for (const auto& p : peers_)
        if (p.id == id)
            return true;
    return false;
}
void SessionRouter::members(std::uint64_t lobby, const std::vector<std::uint64_t>& ids) {
    if (std::find(ids.begin(), ids.end(), self_) == ids.end() || ids.size() > 8)
        lobby = 0;
    if (lobby != lobby_) {
        reset(DeliveryReason::SessionEnded);
        peers_.clear();
        lobby_ = lobby;
        ++nonce_;
    }
    if (!lobby_)
        return;
    for (const auto& p : peers_)
        if (std::find(ids.begin(), ids.end(), p.id) == ids.end())
            prune(p.id, DeliveryReason::PeerChanged);
    peers_.erase(std::remove_if(peers_.begin(), peers_.end(),
                                [&](const Peer& p) {
                                    return std::find(ids.begin(), ids.end(), p.id) == ids.end();
                                }),
                 peers_.end());
    for (auto id : ids)
        if (id && id != self_ && !find(id) && peers_.size() < 7)
            peers_.push_back({id, 0, 0, 0, false, false, next_label_++});
}
void SessionRouter::character(std::uint64_t identity) {
    // Zero means temporarily unreadable, not a new character. Retain the last
    // known identity through loading; a different real identity cancels queues.
    if (identity && identity != character_) {
        reset(DeliveryReason::CharacterChanged);
        character_ = identity;
        ++nonce_;
    }
}
void SessionRouter::set_shop_sharing(bool enabled) {
    share_shops_ = enabled;
}
void SessionRouter::world(bool ready) {
    // Readiness is a pause, never a session reset. Stable nonces and sequence
    // numbers preserve pending items and replay protection through respawns.
    if (world_ != ready) {
        world_ = ready;
        last_hello_ = 0;
    }
}
bool SessionRouter::enqueue(Peer& peer, unsigned kind, std::uint64_t seq, const AwardPacket* packet,
                            DeliveryReason reason) {
    if (outgoing_.size() >= 1024)
        return false;
    RoutedMessage m{};
    m.peer = peer.id;
    auto* p = m.wire.bytes.data();
    std::memcpy(p, "SLS4", 4);
    p[5] = shared_protocol_version;
    p[6] = static_cast<unsigned char>(reason);
    p[7] = static_cast<unsigned char>(kind);
    put64(p + 8, lobby_);
    put64(p + 16, nonce_);
    put64(p + 24, kind == 1 ? 0 : peer.nonce);
    put64(p + 32, seq);
    put32(p + 40, packet ? packet->count : 0);
    put32(p + 44, world_ ? 1 : 0);
    if (packet)
        for (unsigned i = 0; i < packet->count; ++i) {
            const auto& e = packet->entries[i];
            auto* q = p + 48 + 16 * i;
            put32(q, e.item);
            put32(q + 4, e.quantity);
            put32(q + 8, e.unknown);
            put32(q + 12, e.gem);
        }
    outgoing_.push_back(m);
    return true;
}
void SessionRouter::tick(std::uint64_t now) {
    now_ = now;
    for (auto& p : peers_)
        if (p.ack && now >= p.last_seen && now - p.last_seen > 6000)
            p.ack = false;
    // Completion warnings are diagnostic only. Never resend or reapply awards.
    for (auto& p : outstanding_)
        if (!p.warned && now >= p.offered_at && now - p.offered_at >= 120000) {
            p.warned = true;
            event(DeliveryAction::CompletionMissing, DeliveryReason::MissingCompletion,
                  {p.peer, nonce_, p.offered_at, p.packet, p.sequence, p.shop_purchase});
        }
    if (!lobby_ || now - last_hello_ < 1000)
        return;
    last_hello_ = now;
    for (auto& p : peers_)
        enqueue(p, 1, 0, nullptr);
}
unsigned SessionRouter::capture(const AwardPacket& packet, std::uint64_t now, bool shop_purchase) {
    now_ = now;
    if (!world_ || !lobby_ || !valid_shared_packet(packet) || (shop_purchase && !share_shops_))
        return 0;
    unsigned count = 0;
    const auto seq = sequence_++;
    for (auto& p : peers_) {
        // A known addon peer remains eligible while loading or heartbeat-stale.
        // Never send history to a peer that has not yet identified its session.
        if (!p.nonce)
            continue;
        Delivery d{p.id, nonce_, now, packet, seq, shop_purchase};
        if (outstanding_.size() >= 1024 || !enqueue(p, shop_purchase ? 6 : 3, seq, &packet)) {
            event(DeliveryAction::Unresolved, DeliveryReason::TrackingFull, d);
            continue;
        }
        outstanding_.push_back({p.id, p.nonce, seq, now, packet, false, shop_purchase});
        event(DeliveryAction::Offered, DeliveryReason::None, d);
        ++count;
        ++counters_.sent;
    }
    return count;
}
bool SessionRouter::receive(std::uint64_t id, const void* bytes, std::size_t length,
                            std::uint64_t now) {
    now_ = now;
    auto* peer = find(id);
    auto reject = [&](DeliveryReason reason = DeliveryReason::Malformed,
                      std::uint64_t generation = 0, std::uint64_t seq = 0) {
        ++counters_.rejected;
        event(DeliveryAction::Rejected, reason, {id, generation, now, {}, seq});
        return false;
    };
    if (!peer || !lobby_ || !bytes || length != shared_wire_size)
        return reject();
    const auto* p = static_cast<const unsigned char*>(bytes);
    if (std::memcmp(p, "SLS4", 4) || p[4] || p[5] != shared_protocol_version ||
        get64(p + 8) != lobby_ || !get64(p + 16) || get32(p + 44) > 1)
        return reject();
    unsigned kind = p[7];
    auto nonce = get64(p + 16), target = get64(p + 24), seq = get64(p + 32);
    auto count = get32(p + 40);
    bool ready = get32(p + 44) == 1;
    auto reason = static_cast<DeliveryReason>(p[6]);
    if (kind == 1) {
        if (target || seq || count || p[6])
            return reject();
        if (peer->nonce != nonce) {
            if (peer->nonce)
                prune(id, DeliveryReason::PeerChanged);
            peer->nonce = nonce;
            peer->last_sequence = 0;
            peer->ack = false;
        }
        peer->world = ready;
        peer->last_seen = now;
        enqueue(*peer, 2, 0, nullptr);
        return true;
    }
    if (target != nonce_ || !peer->nonce || nonce != peer->nonce)
        return reject();
    if (kind == 2) {
        if (seq || count || p[6])
            return reject();
        peer->ack = true;
        peer->world = ready;
        peer->last_seen = now;
        return true;
    }
    if (kind == 4 || kind == 5) {
        const bool valid_reason = kind == 4 ? reason == DeliveryReason::Verified
                                            : (reason == DeliveryReason::NoChange ||
                                               reason == DeliveryReason::PartialOrUnexpected ||
                                               reason == DeliveryReason::VerificationUnavailable ||
                                               reason == DeliveryReason::BufferFull ||
                                               reason == DeliveryReason::ShopSharingDisabled);
        if (!seq || count || !valid_reason)
            return reject();
        auto it = std::find_if(outstanding_.begin(), outstanding_.end(), [&](const Outstanding& o) {
            return o.peer == id && o.peer_nonce == nonce && o.sequence == seq;
        });
        if (it == outstanding_.end())
            return reject(DeliveryReason::Duplicate, nonce, seq);
        event(kind == 4 ? DeliveryAction::CompletionReceived : DeliveryAction::FailureReceived,
              reason, {id, nonce_, it->offered_at, it->packet, seq, it->shop_purchase});
        if (kind == 4)
            ++counters_.confirmed_by_peer;
        else
            ++counters_.failed_by_peer;
        outstanding_.erase(it);
        peer->last_seen = now;
        return true;
    }
    if ((kind != 3 && kind != 6) || !seq || p[6] || count > 10)
        return reject();
    if (seq <= peer->last_sequence)
        return reject(DeliveryReason::Duplicate, nonce, seq);
    AwardPacket packet{};
    packet.count = count;
    for (unsigned i = 0; i < count; ++i) {
        const auto* q = p + 48 + 16 * i;
        packet.entries[i] = {get32(q), get32(q + 4), get32(q + 8), get32(q + 12)};
    }
    if (!valid_shared_packet(packet))
        return reject();
    Delivery d{id, nonce, now, packet, seq, kind == 6};
    peer->last_sequence = seq;
    peer->last_seen = now;
    if (kind == 6 && !share_shops_) {
        ++counters_.unresolved;
        event(DeliveryAction::Unresolved, DeliveryReason::ShopSharingDisabled, d);
        if (!enqueue(*peer, 5, seq, nullptr, DeliveryReason::ShopSharingDisabled))
            event(DeliveryAction::SendFailed, DeliveryReason::TrackingFull, d);
        return false;
    }
    if (incoming_.size() >= delivery_capacity) {
        ++counters_.unresolved;
        event(DeliveryAction::Unresolved, DeliveryReason::BufferFull, d);
        if (!enqueue(*peer, 5, seq, nullptr, DeliveryReason::BufferFull))
            event(DeliveryAction::SendFailed, DeliveryReason::TrackingFull, d);
        return false;
    }
    incoming_.push_back({d, false, false});
    ++counters_.received;
    event(DeliveryAction::Buffered, DeliveryReason::None, d);
    return true;
}
bool SessionRouter::take_send(RoutedMessage& out) {
    if (outgoing_.empty())
        return false;
    out = outgoing_.front();
    outgoing_.pop_front();
    return true;
}
void SessionRouter::send_result(const RoutedMessage& m, int result, std::uint64_t now) {
    now_ = now;
    if (result == 1 || m.wire.bytes[7] < 3)
        return;
    auto seq = get64(m.wire.bytes.data() + 32);
    event(
        DeliveryAction::SendFailed, DeliveryReason::SteamSendFailed,
        {m.peer,
         (m.wire.bytes[7] == 3 || m.wire.bytes[7] == 6) ? nonce_ : get64(m.wire.bytes.data() + 24),
         now,
         {},
         seq},
        result);
    if (m.wire.bytes[7] == 3 || m.wire.bytes[7] == 6)
        outstanding_.erase(std::remove_if(outstanding_.begin(), outstanding_.end(),
                                          [&](const Outstanding& o) {
                                              return o.peer == m.peer && o.sequence == seq;
                                          }),
                           outstanding_.end());
}
bool SessionRouter::take_delivery(Delivery& out, std::uint64_t now) {
    now_ = now;
    if (!world_ || !lobby_ || incoming_.empty() || incoming_.front().applying)
        return false;
    auto& p = incoming_.front();
    p.applying = true;
    out = p.value;
    return true;
}
bool SessionRouter::defer_delivery(const Delivery& d, std::uint64_t now) {
    now_ = now;
    if (incoming_.empty())
        return false;
    auto& p = incoming_.front();
    if (!p.applying || p.value.peer != d.peer || p.value.nonce != d.nonce ||
        p.value.sequence != d.sequence)
        return false;
    p.applying = false;
    if (!p.deferred_logged) {
        event(DeliveryAction::Deferred, DeliveryReason::InventoryUnavailable, d);
        p.deferred_logged = true;
    }
    return true;
}
bool SessionRouter::finish_delivery(const Delivery& d, DeliveryReason reason, std::uint64_t now) {
    now_ = now;
    if (incoming_.empty())
        return false;
    const auto& p = incoming_.front();
    if (!p.applying || p.value.peer != d.peer || p.value.nonce != d.nonce ||
        p.value.sequence != d.sequence)
        return false;
    if (reason != DeliveryReason::Verified && reason != DeliveryReason::NoChange &&
        reason != DeliveryReason::PartialOrUnexpected &&
        reason != DeliveryReason::VerificationUnavailable)
        return false;
    bool ok = reason == DeliveryReason::Verified;
    event(ok ? DeliveryAction::Completed : DeliveryAction::Unresolved, reason, d);
    if (ok)
        ++counters_.completed;
    else
        ++counters_.unresolved;
    if (auto* peer = find(d.peer))
        if (!enqueue(*peer, ok ? 4 : 5, d.sequence, nullptr, reason))
            event(DeliveryAction::SendFailed, DeliveryReason::TrackingFull, d);
    incoming_.pop_front();
    return true;
}
bool SessionRouter::take_event(DeliveryEvent& out) {
    if (events_.empty())
        return false;
    out = events_.front();
    events_.pop_front();
    return true;
}
RouterStats SessionRouter::stats() const {
    auto s = counters_;
    s.members = static_cast<unsigned>(peers_.size());
    s.pending = static_cast<unsigned>(incoming_.size());
    s.awaiting_completion = static_cast<unsigned>(outstanding_.size());
    for (const auto& p : peers_)
        if (p.ack && p.world && world_)
            ++s.ready;
    return s;
}
} // namespace shared_loot

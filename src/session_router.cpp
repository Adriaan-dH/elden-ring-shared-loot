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
void SessionRouter::initialize(std::uint64_t self, std::uint64_t nonce) {
    self_ = self;
    nonce_ = nonce ? nonce : 1;
    reset();
}
// Drop transient traffic and handshakes whenever the lobby/world changes.
// No old grants are replayed to a new character, session, or late joiner.
void SessionRouter::reset() {
    outgoing_.clear();
    incoming_.clear();
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
    for (auto& p : peers_)
        if (p.id == id)
            return true;
    return false;
}
// Steam supplies the authoritative membership list. Keep only current peers
// and discard queued traffic for players who left. Eight is the current addon cap.
void SessionRouter::members(std::uint64_t lobby, const std::vector<std::uint64_t>& ids) {
    bool self_present = std::find(ids.begin(), ids.end(), self_) != ids.end();
    if (!self_present || ids.size() > 8)
        lobby = 0;
    if (lobby != lobby_) {
        lobby_ = lobby;
        ++nonce_;
        peers_.clear();
        reset();
    }
    if (!lobby_) {
        peers_.clear();
        return;
    }
    peers_.erase(std::remove_if(peers_.begin(), peers_.end(),
                                [&](const Peer& p) {
                                    return std::find(ids.begin(), ids.end(), p.id) == ids.end();
                                }),
                 peers_.end());
    for (auto id : ids)
        if (id && id != self_ && !find(id) && peers_.size() < 7)
            peers_.push_back({id, 0, 0, 0, false, false});
    incoming_.erase(std::remove_if(incoming_.begin(), incoming_.end(),
                                   [&](const Delivery& d) { return !member(d.peer); }),
                    incoming_.end());
    outgoing_.erase(std::remove_if(outgoing_.begin(), outgoing_.end(),
                                   [&](const RoutedMessage& m) { return !member(m.peer); }),
                    outgoing_.end());
}
void SessionRouter::world(bool ready) {
    if (world_ != ready) {
        world_ = ready;
        ++nonce_;
        reset();
    }
}
// Fixed 208-byte frame: magic/version/kind, lobby, sender/target nonces,
// sequence, count, world-ready flag, then ten 16-byte item slots.
// Kinds: 1 = hello, 2 = acknowledgement, 3 = acquired-item bundle.
bool SessionRouter::enqueue(Peer& peer, unsigned kind, std::uint64_t seq,
                            const AwardPacket* packet) {
    if (outgoing_.size() >= 256) {
        ++counters_.rejected;
        return false;
    }
    RoutedMessage message{};
    message.peer = peer.id;
    auto* p = message.wire.bytes.data();
    std::memcpy(p, "SLS4", 4);
    p[5] = 1;
    p[7] = static_cast<unsigned char>(kind);
    put64(p + 8, lobby_);
    put64(p + 16, nonce_);
    put64(p + 24, kind == 1 ? 0 : peer.nonce);
    put64(p + 32, seq);
    put32(p + 40, packet ? packet->count : 0);
    put32(p + 44, world_ ? 1 : 0);
    if (packet)
        for (unsigned i = 0; i < packet->count; ++i) {
            auto& e = packet->entries[i];
            auto* q = p + 48 + 16 * i;
            put32(q, e.item);
            put32(q + 4, e.quantity);
            put32(q + 8, e.unknown);
            put32(q + 12, e.gem);
        }
    outgoing_.push_back(message);
    return true;
}
// Periodic hellos advertise readiness even when nobody collects an item.
// A silent peer must complete the handshake again before receiving more loot.
void SessionRouter::tick(std::uint64_t now) {
    for (auto& p : peers_)
        if (p.ack && now - p.last_seen > 6000)
            p.ack = false;
    if (!lobby_ || now - last_hello_ < 1000)
        return;
    last_hello_ = now;
    for (auto& p : peers_)
        enqueue(p, 1, 0, nullptr);
}
// Fan out to every ready participant directly; the Seamless host is not a
// special relay. Each acquisition gets one sequence shared across its recipients.
unsigned SessionRouter::capture(const AwardPacket& packet, std::uint64_t) {
    if (!world_ || !lobby_ || !valid_shared_packet(packet))
        return 0;
    unsigned count = 0;
    auto seq = sequence_++;
    for (auto& p : peers_)
        if (p.ack && p.world && enqueue(p, 3, seq, &packet)) {
            ++count;
            ++counters_.sent;
        }
    return count;
}
// Validate membership, session identity and shape before decoding items.
// Nonces reject earlier sessions; per-sender sequences reject repeated grants.
bool SessionRouter::receive(std::uint64_t id, const void* bytes, std::size_t length,
                            std::uint64_t now) {
    auto reject = [&]() {
        ++counters_.rejected;
        return false;
    };
    auto* peer = find(id);
    if (!peer || !lobby_ || !bytes || length != shared_wire_size)
        return reject();
    auto* p = static_cast<const unsigned char*>(bytes);
    if (std::memcmp(p, "SLS4", 4) || p[4] || p[5] != 1 || p[6] || get64(p + 8) != lobby_ ||
        !get64(p + 16) || get32(p + 44) > 1)
        return reject();
    unsigned kind = p[7];
    auto nonce = get64(p + 16), target = get64(p + 24), seq = get64(p + 32);
    auto count = get32(p + 40);
    bool world = get32(p + 44) == 1;
    if (kind == 1) {
        if (target || seq || count)
            return reject();
        if (peer->nonce != nonce) {
            peer->nonce = nonce;
            peer->last_sequence = 0;
            peer->ack = false;
            incoming_.erase(std::remove_if(incoming_.begin(), incoming_.end(),
                                           [&](const Delivery& d) { return d.peer == id; }),
                            incoming_.end());
        }
        peer->world = world;
        peer->last_seen = now;
        enqueue(*peer, 2, 0, nullptr);
        return true;
    }
    if (target != nonce_ || !peer->nonce || nonce != peer->nonce)
        return reject();
    if (kind == 2) {
        if (seq || count)
            return reject();
        peer->ack = true;
        peer->world = world;
        peer->last_seen = now;
        return true;
    }
    if (kind != 3 || !world_ || !world || !peer->world || !peer->ack || !seq ||
        seq <= peer->last_sequence || count > 10)
        return reject();
    AwardPacket packet{};
    packet.count = count;
    for (unsigned i = 0; i < count; ++i) {
        auto* q = p + 48 + 16 * i;
        packet.entries[i] = {get32(q), get32(q + 4), get32(q + 8), get32(q + 12)};
    }
    if (!valid_shared_packet(packet) || incoming_.size() >= 128)
        return reject();
    peer->last_sequence = seq;
    peer->last_seen = now;
    incoming_.push_back({id, nonce, now, packet});
    ++counters_.received;
    return true;
}
bool SessionRouter::take_send(RoutedMessage& out) {
    if (outgoing_.empty())
        return false;
    out = outgoing_.front();
    outgoing_.pop_front();
    return true;
}
// Deliver only fresh grants from peers still in this world/session.
// Pending awards expire after 15 seconds rather than becoming catch-up history.
bool SessionRouter::take_delivery(Delivery& out, std::uint64_t now) {
    if (!world_ || !lobby_)
        return false;
    while (!incoming_.empty()) {
        auto d = incoming_.front();
        incoming_.pop_front();
        auto* p = find(d.peer);
        if (p && p->ack && p->world && p->nonce == d.nonce && now - d.received_at < 15000) {
            out = d;
            return true;
        }
    }
    return false;
}
RouterStats SessionRouter::stats() const {
    auto s = counters_;
    s.members = static_cast<unsigned>(peers_.size());
    s.pending = static_cast<unsigned>(incoming_.size());
    for (auto& p : peers_)
        if (p.ack && p.world && world_)
            ++s.ready;
    return s;
}
} // namespace shared_loot

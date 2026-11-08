#include "session_router.h"
#include "source_filter.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                   \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
using namespace shared_loot;
int main() {
    SessionRouter a, b, c;
    a.initialize(101, 10001);
    b.initialize(202, 20001);
    c.initialize(303, 30001);
    std::vector<std::uint64_t> ids{101, 202, 303};
    for (auto* p : {&a, &b, &c}) {
        p->members(456, ids);
        p->world(true);
    }
    auto pump = [&]() {
        for (int round = 0; round < 10; ++round)
            for (auto* p : {&a, &b, &c}) {
                RoutedMessage m{};
                while (p->take_send(m)) {
                    auto* target = m.peer == 101 ? &a : m.peer == 202 ? &b : &c;
                    target->receive(p == &a   ? 101
                                    : p == &b ? 202
                                              : 303,
                                    m.wire.bytes.data(), m.wire.bytes.size(), 2000);
                }
            }
    };
    for (auto* p : {&a, &b, &c})
        p->tick(2000);
    pump();
    CHECK(a.stats().ready == 2 && b.stats().ready == 2 && c.stats().ready == 2);
    AwardPacket loot{};
    loot.count = 4;
    loot.entries[0] = {0x400050f0, 3, 0xffffffff, 0xffffffff};
    loot.entries[1] = {0x002e3bf0, 1, 0xffffffff, 10002};
    loot.entries[2] = {0x1019f104, 1, 0xffffffff, 0xffffffff};
    loot.entries[3] = {0x80002710, 1, 0xffffffff, 0xffffffff};
    CHECK(broad_pickup_source(true, false, 0x55ce2e, 3, loot));
    CHECK(!broad_pickup_source(true, true, 0x55ce2e, 0, loot));
    CHECK(!broad_pickup_source(true, false, 0x56441c, 4, loot));
    CHECK(!broad_pickup_source(true, false, 0x56441c, 0, loot)); // scarab/scripted generic route
    CHECK(a.capture(loot, 2001) == 2);
    pump();
    Delivery delivery{};
    CHECK(!a.take_delivery(delivery, 2001));
    CHECK(b.take_delivery(delivery, 2001));
    CHECK(std::memcmp(&delivery.packet, &loot, sizeof(loot)) == 0);
    CHECK(b.finish_delivery(delivery, DeliveryReason::Verified, 2001));
    CHECK(c.take_delivery(delivery, 2001));
    CHECK(c.finish_delivery(delivery, DeliveryReason::Verified, 2001));
    CHECK(!b.take_delivery(delivery, 2001));
    pump(); // Drain the explicit completion reports before inspecting a pickup frame.
    CHECK(b.capture(loot, 2002) == 2);
    RoutedMessage replay{};
    CHECK(b.take_send(replay));
    CHECK(replay.peer == 101);
    CHECK(a.receive(202, replay.wire.bytes.data(), shared_wire_size, 2002));
    CHECK(!a.receive(202, replay.wire.bytes.data(), shared_wire_size, 2002));
    CHECK(a.take_delivery(delivery, 2003));
    CHECK(a.finish_delivery(delivery, DeliveryReason::Verified, 2003));
    CHECK(!a.take_delivery(delivery, 2003));
    CHECK(!a.receive(999, replay.wire.bytes.data(), shared_wire_size, 2002));
    CHECK(!a.receive(202, replay.wire.bytes.data(), 20, 2002));
    auto invalid = replay.wire;
    invalid.bytes[5] = 99;
    CHECK(!a.receive(202, invalid.bytes.data(), shared_wire_size, 2002));
    // Loading pauses delivery without destroying the queue or handshake generation.
    CHECK(c.capture(loot, 2004) == 2);
    pump();
    a.world(false);
    CHECK(!a.take_delivery(delivery, 2005));
    CHECK(a.capture(loot, 2005) == 0);
    a.world(true);
    CHECK(a.stats().pending == 1);
    for (auto* p : {&a, &b, &c})
        p->tick(4000);
    pump();
    CHECK(a.stats().ready == 2);
    CHECK(a.take_delivery(delivery, 4001));
    CHECK(a.finish_delivery(delivery, DeliveryReason::Verified, 4001));
    CHECK(!a.take_delivery(delivery, 4001));
    a.members(456, {101, 202});
    CHECK(!a.member(303));
    CHECK(a.stats().members == 1);
    CHECK(!a.receive(303, replay.wire.bytes.data(), shared_wire_size, 4001));
    a.members(0, {});
    CHECK(a.capture(loot, 4001) == 0);
    CHECK(a.stats().members == 0);
    loot.count = 11;
    CHECK(!valid_shared_packet(loot));
    loot.count = 1;
    loot.entries[0].quantity = 0;
    CHECK(!valid_shared_packet(loot));
    loot.entries[0] = {0x400050f0, 2, 0xffffffff, 0xffffffff};
    CHECK(broad_pickup_source(true, false, 0x56441c, 0, loot));
    // Regression: the uploaded client log had three valid Ruin Fragment x1
    // requests at this exact caller/flag, but 0.4.0 filtered them before send.
    loot.entries[0] = {0x400006e0, 1, 0xffffffff, 0xffffffff};
    CHECK(broad_pickup_source(true, false, 0x56441c, 0, loot));
    CHECK(!broad_pickup_source(true, false, 0x56441c, 4, loot));
    CHECK(!broad_pickup_source(false, true, 0x56441c, 0, loot));
    CHECK(!broad_pickup_source(true, false, 0x123456, 0, loot));
    loot.entries[0].item = 0x40001fde;
    CHECK(!broad_pickup_source(true, false, 0x56441c, 4, loot));
    std::puts("PASS: three symmetric peers, handshake, multiple categories/metadata, no echo, "
              "replay rejection, membership/world gates, source exclusions");
}

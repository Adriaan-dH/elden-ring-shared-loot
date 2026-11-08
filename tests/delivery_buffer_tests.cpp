#include "session_router.h"
#include "recipient_menu_gate.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace shared_loot;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "FAIL buffer line %d: %s\n", __LINE__, #x);                       \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
struct Party {
    SessionRouter a, b;
    std::uint64_t now = 2000;
    Party() {
        a.initialize(101, 10001);
        b.initialize(202, 20001);
        a.character(1);
        b.character(2);
        a.members(456, {101, 202});
        b.members(456, {101, 202});
        a.world(true);
        b.world(true);
        a.tick(now);
        b.tick(now);
        pump();
    }
    void pump() {
        for (unsigned i = 0; i < 8; ++i)
            for (auto* p : {&a, &b}) {
                RoutedMessage m{};
                while (p->take_send(m)) {
                    auto& dest = p == &a ? b : a;
                    dest.receive(p == &a ? 101 : 202, m.wire.bytes.data(), shared_wire_size, now);
                    p->send_result(m, 1, now);
                }
            }
    }
};
AwardPacket loot() {
    AwardPacket p{};
    p.count = 1;
    p.entries[0] = {0x400006e0, 3, 0xffffffff, 0xffffffff};
    return p;
}
void check_menu_buffering() {
    Party p;
    RecipientMenuGate menu;
    Delivery delivery{};
    CHECK(menu.update(true, true, p.now) == MenuGateState::Busy);
    CHECK(p.a.capture(loot(), p.now) == 1);
    p.pump();
    CHECK(p.b.stats().pending == 1);
    // The recipient can send a purchase while its own incoming loot waits.
    CHECK(p.b.capture(loot(), p.now, true) == 1);
    p.pump();
    CHECK(p.a.take_delivery(delivery, p.now) && delivery.shop_purchase);
    p.now += 180000;
    CHECK(menu.update(true, true, p.now) == MenuGateState::Busy);
    CHECK(p.b.stats().pending == 1);
    CHECK(menu.update(true, false, p.now) == MenuGateState::Settling);
    CHECK(menu.update(false, false, p.now + 249) == MenuGateState::Unavailable);
    p.now += 250;
    CHECK(menu.update(true, false, p.now) == MenuGateState::Settling);
    p.now += 250;
    CHECK(menu.update(true, false, p.now) == MenuGateState::Ready);
    CHECK(p.b.take_delivery(delivery, p.now) && !delivery.shop_purchase);
    CHECK(p.b.finish_delivery(delivery, DeliveryReason::Verified, p.now));
    CHECK(!p.b.take_delivery(delivery, p.now));
}
int main() {
    check_menu_buffering();
    auto packet = loot();
    Party p;
    Delivery d{};
    CHECK(p.a.stats().ready == 1);
    p.b.world(false);
    p.now = 3000;
    p.b.tick(p.now);
    p.pump();
    CHECK(p.a.stats().ready == 0);
    CHECK(p.a.capture(packet, p.now) == 1);
    p.pump();
    CHECK(p.b.stats().pending == 1 && !p.b.take_delivery(d, p.now));
    // Five minutes of loading and a stale heartbeat do not delete accepted awards.
    p.now = 303000;
    p.a.tick(p.now);
    p.b.tick(p.now);
    p.pump();
    CHECK(p.b.stats().pending == 1);
    CHECK(!p.b.take_delivery(d, p.now));
    CHECK(p.a.capture(packet, p.now) == 1);
    p.pump();
    CHECK(p.b.stats().pending == 2);
    p.b.world(true);
    CHECK(p.b.take_delivery(d, p.now));
    CHECK(!p.b.take_delivery(d, p.now)); // an active lease cannot be granted twice
    CHECK(p.b.defer_delivery(d, p.now));
    CHECK(p.b.take_delivery(d, p.now));
    CHECK(p.b.finish_delivery(d, DeliveryReason::Verified, p.now));
    p.pump();
    CHECK(p.a.stats().confirmed_by_peer == 1);
    CHECK(p.b.take_delivery(d, p.now));
    CHECK(p.b.finish_delivery(d, DeliveryReason::PartialOrUnexpected, p.now));
    p.pump();
    CHECK(p.a.stats().failed_by_peer == 1 && !p.b.take_delivery(d, p.now));
    CHECK(!p.b.finish_delivery(d, DeliveryReason::Verified, p.now));
    // Drop the completion envelope deliberately: no timeout or heartbeat resends loot.
    CHECK(p.a.capture(packet, p.now) == 1);
    p.pump();
    CHECK(p.b.take_delivery(d, p.now));
    CHECK(p.b.finish_delivery(d, DeliveryReason::Verified, p.now));
    RoutedMessage report{};
    CHECK(p.b.take_send(report) && report.wire.bytes[7] == 4);
    p.now += 600000;
    p.a.tick(p.now);
    p.b.tick(p.now);
    p.pump();
    CHECK(p.a.stats().awaiting_completion == 1);
    CHECK(p.b.stats().pending == 0);
    CHECK(p.a.stats().sent == 3 && p.b.stats().completed == 2);
    // Transport failure is logged and terminal, never automatically retried.
    CHECK(p.a.capture(packet, p.now) == 1);
    RoutedMessage failed{};
    CHECK(p.a.take_send(failed));
    CHECK(failed.wire.bytes[7] == 3);
    p.a.send_result(failed, 8, p.now);
    p.now += 10000;
    p.a.tick(p.now);
    p.pump();
    CHECK(p.b.stats().pending == 0);
    // Duplicate and incompatible offers never produce a second queue entry.
    CHECK(p.a.capture(packet, p.now) == 1);
    RoutedMessage offer{};
    CHECK(p.a.take_send(offer));
    CHECK(p.b.receive(101, offer.wire.bytes.data(), shared_wire_size, p.now));
    CHECK(!p.b.receive(101, offer.wire.bytes.data(), shared_wire_size, p.now));
    auto old = offer;
    old.wire.bytes[5] = 2;
    CHECK(!p.b.receive(101, old.wire.bytes.data(), shared_wire_size, p.now));
    CHECK(p.b.stats().pending == 1);
    p.b.world(false);
    p.b.world(true);
    CHECK(p.b.stats().pending == 1);
    p.b.character(99);
    CHECK(p.b.stats().pending == 0); // never spill into another character
    CHECK(!p.b.receive(101, offer.wire.bytes.data(), shared_wire_size, p.now));
    Party origin;
    CHECK(origin.a.capture(packet, origin.now) == 1);
    origin.pump();
    origin.a.world(false);
    origin.now += 1000;
    origin.a.tick(origin.now);
    origin.pump();
    CHECK(origin.b.stats().ready == 0);
    CHECK(origin.b.take_delivery(d, origin.now)); // sender loading must not block an accepted award
    CHECK(origin.b.finish_delivery(d, DeliveryReason::Verified, origin.now));
    origin.pump();
    CHECK(origin.a.stats().confirmed_by_peer == 1);
    Party shops;
    shops.a.set_shop_sharing(false);
    CHECK(shops.a.capture(packet, shops.now, true) == 0);
    CHECK(shops.a.capture(packet, shops.now) == 1);
    shops.pump();
    CHECK(shops.b.take_delivery(d, shops.now) && !d.shop_purchase);
    CHECK(shops.b.finish_delivery(d, DeliveryReason::Verified, shops.now));
    shops.pump();
    shops.a.set_shop_sharing(true);
    shops.b.set_shop_sharing(false);
    CHECK(shops.a.capture(packet, shops.now, true) == 1);
    shops.pump();
    CHECK(shops.b.stats().pending == 0 && shops.a.stats().failed_by_peer == 1);
    shops.b.set_shop_sharing(true);
    shops.b.world(false);
    CHECK(shops.a.capture(packet, shops.now, true) == 1);
    shops.pump();
    CHECK(shops.b.stats().pending == 1 && !shops.b.take_delivery(d, shops.now));
    shops.b.world(true);
    CHECK(shops.b.take_delivery(d, shops.now) && d.shop_purchase);
    CHECK(shops.b.finish_delivery(d, DeliveryReason::Verified, shops.now));
    shops.pump();
    CHECK(shops.a.stats().confirmed_by_peer == 2);
    Party full;
    full.b.world(false);
    for (unsigned i = 0; i < delivery_capacity; ++i) {
        CHECK(full.a.capture(packet, full.now) == 1);
        full.pump();
    }
    CHECK(full.a.capture(packet, full.now) == 1);
    full.pump();
    CHECK(full.b.stats().pending == delivery_capacity && full.b.stats().unresolved == 1);
    CHECK(full.a.stats().failed_by_peer == 1);
    full.b.members(0, {});
    CHECK(full.b.stats().pending == 0);
    bool saw_cancel = false;
    DeliveryEvent event{};
    while (full.b.take_event(event))
        saw_cancel = saw_cancel || event.action == DeliveryAction::Cancelled;
    CHECK(saw_cancel);
    std::puts("PASS: long-load buffering, stale peers, explicit completion/failure, one attempt, "
              "no resend, deduplication, queue limits and character/session cancellation");
}

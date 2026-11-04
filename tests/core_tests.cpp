#include "shared_loot.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr);                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
struct Inventory {
    SL_Context* context;
    int calls = 0;
    uint32_t item = 0, quantity = 0;
    bool fail = false;
};
int grant(void* ptr, uint32_t item, uint32_t quantity) {
    auto& inventory = *static_cast<Inventory*>(ptr);
    if (inventory.fail)
        return 0;
    ++inventory.calls;
    inventory.item = item;
    inventory.quantity += quantity;
    CHECK(SL_Capture(inventory.context, item, quantity, SL_FLOOR) == SL_IGNORED);
    return 1;
}
int main() {
    CHECK(SL_ApiVersion() == 1);
    CHECK(!SL_Create(0));
    auto* a = SL_Create(101);
    auto* b = SL_Create(202);
    CHECK(a && b);
    Inventory inventory{b};
    SL_Event event{};
    CHECK(SL_Capture(a, 100, 3, SL_OTHER) == SL_IGNORED);
    CHECK(SL_Capture(a, 100, 0, SL_FLOOR) == SL_INVALID);
    CHECK(SL_Capture(a, 100, 3, SL_FLOOR) == SL_OK);
    CHECK(SL_NextOutgoing(a, &event) == SL_OK);
    CHECK(SL_Receive(a, &event) == SL_IGNORED);
    CHECK(SL_Receive(b, &event) == SL_OK);
    CHECK(SL_Receive(b, &event) == SL_DUPLICATE);
    inventory.fail = true;
    CHECK(SL_ApplyNext(b, grant, &inventory) == SL_GRANT_FAILED);
    inventory.fail = false;
    CHECK(SL_ApplyNext(b, grant, &inventory) == SL_OK);
    CHECK(inventory.calls == 1 && inventory.item == 100 && inventory.quantity == 3);
    CHECK(SL_ApplyNext(b, grant, &inventory) == SL_EMPTY);
    CHECK(SL_NextOutgoing(b, &event) == SL_EMPTY);
    // Another real copy of the same item intentionally shares again.
    CHECK(SL_Capture(a, 100, 3, SL_ENEMY) == SL_OK);
    CHECK(SL_NextOutgoing(a, &event) == SL_OK);
    CHECK(SL_Receive(b, &event) == SL_OK);
    CHECK(SL_ApplyNext(b, grant, &inventory) == SL_OK);
    CHECK(inventory.calls == 2 && inventory.quantity == 6);
    // Late joiners start with no pending grants.
    auto* late = SL_Create(303);
    CHECK(SL_ApplyNext(late, grant, &inventory) == SL_EMPTY);
    // Validate before accepting; don't consume delivery identity on rejection.
    event.sequence = 3;
    event.quantity = 1000;
    CHECK(SL_Receive(b, &event) == SL_INVALID);
    event.quantity = 1;
    CHECK(SL_Receive(b, &event) == SL_OK);
    CHECK(SL_ApplyNext(b, grant, &inventory) == SL_OK);
    // Backpressure retains a failed enqueue's identity for a later retry.
    for (unsigned i = 0; i < 256; ++i) {
        event.sequence = 4 + i;
        CHECK(SL_Receive(b, &event) == SL_OK);
    }
    event.sequence = 260;
    CHECK(SL_Receive(b, &event) == SL_FULL);
    CHECK(SL_ApplyNext(b, grant, &inventory) == SL_OK);
    CHECK(SL_Receive(b, &event) == SL_OK);
    for (unsigned i = 0; i < 256; ++i)
        CHECK(SL_Capture(a, 1, 1, SL_FLOOR) == SL_OK);
    CHECK(SL_Capture(a, 1, 1, SL_FLOOR) == SL_FULL);
    CHECK(SL_Receive(nullptr, &event) == SL_INVALID);
    SL_Destroy(late);
    SL_Destroy(b);
    SL_Destroy(a);
    std::puts("PASS: sharing, filtering, loop suppression, repeat pickups, late joins, validation, "
              "retry, backpressure");
}

#include "inventory_probe.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace shared_loot;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "FAIL inventory line %d: %s\n", __LINE__, #x);                    \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
struct Memory {
    std::vector<unsigned char> data = std::vector<unsigned char>(0x10000);
    template <class T> void put(std::size_t p, T v) {
        std::memcpy(data.data() + p, &v, sizeof(v));
    }
    static bool read(void* c, std::uintptr_t p, void* out, std::size_t n) {
        auto& m = *static_cast<Memory*>(c);
        if (p > m.data.size() || n > m.data.size() - p)
            return false;
        std::memcpy(out, m.data.data() + p, n);
        return true;
    }
    void inventory(std::size_t inv, std::size_t head, bool multiplayer = false) {
        put<std::uint32_t>(inv + 0xc, 16);
        put<std::uintptr_t>(inv + 0x10, head);
        put<std::uint32_t>(inv + 0x18, 2);
        put<std::uint32_t>(inv + 0x1c, 16);
        put<std::uintptr_t>(inv + 0x20, head + 0x100);
        put<std::uint32_t>(inv + 0x28, 1);
        put<std::uint32_t>(inv + 0x2c, 16);
        put<std::uintptr_t>(inv + 0x30, head + 0x200);
        put<std::uint32_t>(inv + 0x38, 1);
        put<std::uintptr_t>(inv + 0x40, head);
        put<std::uintptr_t>(inv + 0x48, inv + 0x18);
        put<std::uintptr_t>(inv + 0x50, head + (multiplayer ? 0x200 : 0x100));
        put<std::uintptr_t>(inv + 0x58, inv + (multiplayer ? 0x38 : 0x28));
    }
    void entry(std::size_t p, std::uint32_t item, std::uint32_t quantity) {
        put(p + 4, item);
        put(p + 8, quantity);
    }
};
struct GrantFixture {
    unsigned reads = 0, calls = 0;
    bool preflight = true, postflight = true;
    std::uint64_t delta = 3;
    static bool snapshot(void* p, const AwardPacket&, InventoryCounts& counts) {
        auto& f = *static_cast<GrantFixture*>(p);
        ++f.reads;
        if ((f.reads == 1 && !f.preflight) || (f.reads == 2 && !f.postflight))
            return false;
        counts = {};
        counts[0] = f.calls ? f.delta : 0;
        return true;
    }
    static bool grant(void* p, const AwardPacket&) {
        ++static_cast<GrantFixture*>(p)->calls;
        return true;
    }
};
int main() {
    Memory m;
    m.inventory(0x100, 0x1000, true);
    m.inventory(0x300, 0x2000);
    AwardPacket p{};
    p.count = 2;
    p.entries[0] = {0x400006e0, 3, 0xffffffff, 0xffffffff};
    p.entries[1] = {0x002e3bf0, 1, 0xffffffff, 123};
    m.entry(0x1000, p.entries[0].item, 5);
    m.entry(0x1018, p.entries[1].item, 1);
    m.entry(0x1100, p.entries[0].item, 999); // inactive key list must not be counted
    m.entry(0x1200, p.entries[0].item, 2);
    m.entry(0x2000, p.entries[0].item, 10);
    m.entry(0x2018, 0xffffffff, 0);
    m.entry(0x2100, 0xffffffff, 0);
    MemoryReader reader{&m, &Memory::read};
    InventoryCounts before{}, after{};
    CHECK(inventory_counts(reader, 0x100, p, before));
    CHECK(inventory_counts(reader, 0x300, p, before));
    CHECK(before[0] == 17 && before[1] == 1);
    m.entry(0x2000, p.entries[0].item, 13);
    m.entry(0x1018, p.entries[1].item, 2);
    CHECK(inventory_counts(reader, 0x100, p, after));
    CHECK(inventory_counts(reader, 0x300, p, after));
    CHECK(verify_inventory_delta(p, before, after) == DeliveryReason::Verified);
    CHECK(verify_inventory_delta(p, before, before) == DeliveryReason::NoChange);
    after[0] -= 1;
    CHECK(verify_inventory_delta(p, before, after) == DeliveryReason::PartialOrUnexpected);
    after[0] = before[0] - 1;
    CHECK(verify_inventory_delta(p, before, after) == DeliveryReason::PartialOrUnexpected);
    p.entries[1] = p.entries[0];
    p.entries[1].quantity = 2;
    before = {};
    after = {};
    after[0] = 3;
    after[1] = 3;
    CHECK(verify_inventory_delta(p, before, after) == DeliveryReason::PartialOrUnexpected);
    after[0] = 5;
    after[1] = 5;
    CHECK(verify_inventory_delta(p, before, after) == DeliveryReason::Verified);
    m.put<std::uint32_t>(0x118, 17);
    InventoryCounts untouched{};
    CHECK(!inventory_counts(reader, 0x100, p, untouched));
    CHECK(untouched[0] == 0);
    m.put<std::uint32_t>(0x118, 2);
    m.put<std::uintptr_t>(0x148, 0x999);
    CHECK(!inventory_counts(reader, 0x100, p, untouched));
    m.put<std::uintptr_t>(0x148, 0x118);
    m.put<std::uintptr_t>(0x110, 0xfffffff0);
    m.put<std::uintptr_t>(0x140, 0xfffffff0);
    CHECK(!inventory_counts(reader, 0x100, p, untouched));
    AwardPacket single{};
    single.count = 1;
    single.entries[0] = {0x400006e0, 3, 0xffffffff, 0xffffffff};
    GrantFixture f;
    auto access = GrantAccess{&f, &GrantFixture::snapshot, &GrantFixture::grant};
    auto result = apply_once(access, single);
    CHECK(result.attempted && result.reason == DeliveryReason::Verified && f.calls == 1);
    f = {};
    f.preflight = false;
    result = apply_once(access, single);
    CHECK(!result.attempted && f.calls == 0);
    f = {};
    f.postflight = false;
    result = apply_once(access, single);
    CHECK(result.attempted && result.reason == DeliveryReason::VerificationUnavailable &&
          f.calls == 1);
    f = {};
    f.delta = 1;
    result = apply_once(access, single);
    CHECK(result.reason == DeliveryReason::PartialOrUnexpected && f.calls == 1);
    f = {};
    f.delta = 0;
    result = apply_once(access, single);
    CHECK(result.reason == DeliveryReason::NoChange && f.calls == 1);
    std::puts("PASS: inventory/storage totals, active-key aliases, bounded reads, "
              "exact/partial/zero deltas, duplicate item IDs");
}

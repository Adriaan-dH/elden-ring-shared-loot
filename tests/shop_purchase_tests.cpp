#include "shop_purchase.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace shared_loot;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "FAIL shop %d: %s\n", __LINE__, #x);                              \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
struct Fixture {
    unsigned calls = 0, reads = 0;
    bool readable = true, commits = true;
    std::uint64_t delta = 6;
    std::uint16_t counter = 65535;
    static bool snapshot(void* p, const AwardPacket&, InventoryCounts& counts,
                         std::uint16_t& counter) {
        auto& f = *static_cast<Fixture*>(p);
        ++f.reads;
        if (!f.readable)
            return false;
        counts = {};
        counts[0] = 20 + (f.calls ? f.delta : 0);
        counter = static_cast<std::uint16_t>(f.counter + (f.calls && f.commits ? 1 : 0));
        return true;
    }
    static std::uintptr_t buy(void* p) {
        ++static_cast<Fixture*>(p)->calls;
        return 0x12345678;
    }
};
struct Memory {
    std::vector<unsigned char> bytes = std::vector<unsigned char>(0x1000);
    template <class T> void put(std::size_t n, T value) {
        std::memcpy(bytes.data() + n, &value, sizeof(value));
    }
    static bool read(void* p, std::uintptr_t n, void* out, std::size_t size) {
        auto& b = static_cast<Memory*>(p)->bytes;
        if (n > b.size() || size > b.size() - n)
            return false;
        std::memcpy(out, b.data() + n, size);
        return true;
    }
};
int main() {
    Memory m;
    m.put<std::uint32_t>(0x14c, 0x400006e0);
    m.put<std::uintptr_t>(0x160, 0x400);
    m.put<std::uint32_t>(0x180, 3);
    m.put<std::uint16_t>(0x41a, 2);
    AwardPacket offer{};
    CHECK(read_shop_offer({&m, &Memory::read}, 0x100, offer));
    CHECK(offer.entries[0].quantity == 6 && offer.entries[0].gem == 0xffffffff);
    Fixture f;
    ShopAccess access{&f, &Fixture::snapshot, &Fixture::buy};
    auto result = observe_purchase(access, offer);
    CHECK(f.calls == 1 && result.native_result == 0x12345678);
    CHECK(result.outcome == ShopOutcome::Shared && result.packet.entries[0].quantity == 6);
    f = {};
    f.delta = 2;
    result = observe_purchase(access, offer);
    CHECK(result.outcome == ShopOutcome::Shared && result.packet.entries[0].quantity == 2);
    f = {};
    f.commits = false;
    result = observe_purchase(access, offer);
    CHECK(f.calls == 1 && result.outcome == ShopOutcome::NotCommitted);
    f = {};
    f.delta = 0;
    result = observe_purchase(access, offer);
    CHECK(f.calls == 1 && result.outcome == ShopOutcome::NoChange);
    f = {};
    f.delta = 99;
    result = observe_purchase(access, offer);
    CHECK(result.outcome == ShopOutcome::UnexpectedDelta && f.calls == 1);
    f = {};
    f.readable = false;
    result = observe_purchase(access, offer);
    CHECK(f.calls == 1 && result.outcome == ShopOutcome::Unavailable);
    m.put<std::uint32_t>(0x14c, 0x002e3bf0);
    m.put<std::uint8_t>(0x417, 5);
    m.put<std::uint32_t>(0x17c, 0x80002712);
    CHECK(read_shop_offer({&m, &Memory::read}, 0x100, offer));
    CHECK(offer.entries[0].gem == 10002);
    m.put<std::uint32_t>(0x17c, 0x40000001);
    CHECK(!read_shop_offer({&m, &Memory::read}, 0x100, offer));
    m.put<std::uint32_t>(0x17c, 0xffffffff);
    m.put<std::uint32_t>(0x180, 0);
    CHECK(!read_shop_offer({&m, &Memory::read}, 0x100, offer));
    std::puts(
        "PASS: committed purchase only, exact/bundled/partial quantities, zero/failed/unreadable "
        "transactions, raw return preservation and equipment gem metadata");
}

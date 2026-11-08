#include "pickup_source.h"
#include "source_filter.h"
#include <array>
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
namespace {
struct Object {
    std::array<unsigned char, 0x40> prefix{};
    bool readable = true;
    unsigned reads = 0;
    static constexpr std::uintptr_t base = 0x10000;
    void set(std::uint32_t handle, std::uint16_t flags) {
        std::memcpy(prefix.data(), &handle, sizeof(handle));
        std::memcpy(prefix.data() + 0x30, &flags, sizeof(flags));
    }
    PickupEvidence collect(bool game = true, std::uintptr_t caller = 0x55ce2e,
                           std::uintptr_t packet = base + 0x40,
                           std::uintptr_t notification = base + 0xe4) {
        MemoryReader reader{
            this, [](void* context, std::uintptr_t address, void* out, std::size_t size) {
                auto& object = *static_cast<Object*>(context);
                ++object.reads;
                if (!object.readable || address != base || size != object.prefix.size())
                    return false;
                std::memcpy(out, object.prefix.data(), size);
                return true;
            }};
        return read_pickup_source(reader, game, caller, packet, notification);
    }
};
} // namespace
int main() {
    Object object;
    object.set(0x123451, 0x140);
    const auto own_left = object.collect();
    CHECK(own_left.source == PickupSource::PlayerLeft);
    CHECK(!pickup_source_allows_share(own_left));
    CHECK(own_left.object_handle == 0x123451 && own_left.record_flags == 0x140);
    // No collector or creator identity participates: the same object is blocked
    // if collected by its owner or another player, now or much later.
    for (unsigned unrelated_pickups = 0; unrelated_pickups < 10000; ++unrelated_pickups) {
        Object unrelated;
        unrelated.set(unrelated_pickups << 4, 0x40);
        CHECK(pickup_source_allows_share(unrelated.collect()));
    }
    CHECK(!pickup_source_allows_share(object.collect()));
    // Handle reuse cannot poison another item: evidence is read afresh. Native
    // overflow bags use a zero drop-sharing argument, unlike inventory Leave.
    object.set(0x123451, 0x40);
    CHECK(object.collect().source == PickupSource::WorldPickup);
    CHECK(pickup_source_allows_share(object.collect()));
    // Test the precise bit, not all runtime bags or all high flag bits.
    for (unsigned flags = 0; flags <= 0xffff; ++flags) {
        object.set(1, static_cast<std::uint16_t>(flags));
        CHECK(pickup_source_allows_share(object.collect()) == !(flags & 0x100));
    }
    // Copy semantics: native consumption cannot change a captured decision.
    object.set(1, 0x100);
    const auto captured = object.collect();
    object.set(2, 0);
    CHECK(!pickup_source_allows_share(captured));
    CHECK(pickup_source_allows_share(object.collect()));
    object.readable = false;
    CHECK(object.collect().source == PickupSource::Unavailable);
    CHECK(!pickup_source_allows_share(object.collect()));
    object.readable = true;
    CHECK(object.collect(true, 0x55ce2e, Object::base + 0x40, 0).source ==
          PickupSource::Unavailable);
    CHECK(object.collect(true, 0x55ce2e, 0, 0).source == PickupSource::Unavailable);
    CHECK(object.collect(true, 0x55ce2e, UINTPTR_MAX, 0).source == PickupSource::Unavailable);
    const unsigned reads = object.reads;
    CHECK(object.collect(false).source == PickupSource::NotWorldRecord);
    CHECK(object.collect(true, 0x56441c).source == PickupSource::NotWorldRecord);
    CHECK(object.reads == reads); // never dereference a prefix on another ABI
    AwardPacket packet{};
    packet.count = 1;
    packet.entries[0] = {0x400006c2, 1, 0xffffffff, 0xffffffff};
    CHECK(broad_pickup_source(true, false, 0x55ce2e, 0, packet));
    CHECK(!pickup_source_allows_share(captured));
    CHECK(!shareable_pickup_source(true, false, 0x55ce2e, 0, packet, captured));
    CHECK(shareable_pickup_source(true, false, 0x55ce2e, 0, packet, object.collect()));
    CHECK(!shareable_pickup_source(true, true, 0x55ce2e, 0, packet, object.collect()));
    CHECK(!shareable_pickup_source(true, false, 0x55ce2e, 0, packet, {}));
    object.readable = false;
    CHECK(!shareable_pickup_source(true, false, 0x55ce2e, 0, packet, object.collect()));
    packet.entries[0].item = 0x400050f0;
    CHECK(shareable_pickup_source(true, false, 0x56441c, 0, packet, {}));
    CHECK(!shareable_pickup_source(true, false, 0x56441c, 4, packet, {}));
    CHECK(!broad_pickup_source(true, true, 0x55ce2e, 0, packet));
    std::puts("PASS: object source marker, delayed/other collector, handle reuse, bounded reads "
              "and fail-closed policy");
}

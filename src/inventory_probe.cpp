#include "inventory_probe.h"
#include <cstring>
#include <vector>
namespace shared_loot {
namespace {
template <class T> T field(const unsigned char* header, std::size_t offset) {
    T result{};
    std::memcpy(&result, header + offset, sizeof(result));
    return result;
}
struct Entry {
    std::uint32_t handle, item, quantity, sort;
    std::uint32_t flags, pot_group;
};
static_assert(sizeof(Entry) == 24);
} // namespace
bool inventory_counts(const MemoryReader& reader, std::uintptr_t inventory,
                      const AwardPacket& packet, InventoryCounts& counts) {
    if (!reader.read || !inventory || !valid_shared_packet(packet))
        return false;
    unsigned char header[0x60]{};
    if (!reader.read(reader.context, inventory, header, sizeof(header)))
        return false;
    const auto normal = field<std::uintptr_t>(header, 0x40);
    const auto keys = field<std::uintptr_t>(header, 0x50);
    const auto normal_length = field<std::uintptr_t>(header, 0x48);
    const auto key_length = field<std::uintptr_t>(header, 0x58);
    const auto key_primary = field<std::uintptr_t>(header, 0x20);
    const auto key_multiplayer = field<std::uintptr_t>(header, 0x30);
    // Validate accessor aliases. A readable but stale/random pointer is not an
    // inventory snapshot. Never count the inactive key list a second time.
    if (normal != field<std::uintptr_t>(header, 0x10) || normal_length != inventory + 0x18)
        return false;
    bool primary = keys == key_primary && key_length == inventory + 0x28;
    bool multiplayer = keys == key_multiplayer && key_length == inventory + 0x38;
    if (!primary && !multiplayer)
        return false;
    InventoryCounts additions{};
    for (unsigned list = 0; list < 2; ++list) {
        std::uintptr_t head = list ? keys : normal;
        std::uintptr_t length_pointer = list ? key_length : normal_length;
        auto capacity = field<std::uint32_t>(header, list ? (primary ? 0x1c : 0x2c) : 0x0c);
        std::uint32_t length = 0;
        if (capacity > 16384 ||
            !reader.read(reader.context, length_pointer, &length, sizeof(length)) ||
            length > capacity)
            return false;
        if (!length)
            continue;
        if (!head || head > UINTPTR_MAX - static_cast<std::size_t>(length) * sizeof(Entry))
            return false;
        std::vector<Entry> entries(length);
        if (!reader.read(reader.context, head, entries.data(), entries.size() * sizeof(Entry)))
            return false;
        for (const auto& e : entries) {
            if (e.item == 0xffffffff)
                continue;
            if (e.quantity > 999999)
                return false;
            for (unsigned i = 0; i < packet.count; ++i)
                if (e.item == packet.entries[i].item)
                    additions[i] += e.quantity;
        }
    }
    for (unsigned i = 0; i < packet.count; ++i)
        counts[i] += additions[i];
    return true;
}
DeliveryReason verify_inventory_delta(const AwardPacket& packet, const InventoryCounts& before,
                                      const InventoryCounts& after) {
    if (!valid_shared_packet(packet))
        return DeliveryReason::VerificationUnavailable;
    bool unchanged = true, verified = true;
    for (unsigned i = 0; i < packet.count; ++i) {
        bool first = true;
        for (unsigned j = 0; j < i; ++j)
            if (packet.entries[j].item == packet.entries[i].item)
                first = false;
        if (!first)
            continue;
        std::uint64_t expected = 0;
        for (unsigned j = i; j < packet.count; ++j)
            if (packet.entries[j].item == packet.entries[i].item)
                expected += packet.entries[j].quantity;
        unchanged = unchanged && after[i] == before[i];
        verified = verified && after[i] >= before[i] && after[i] - before[i] == expected;
    }
    if (verified)
        return DeliveryReason::Verified;
    return unchanged ? DeliveryReason::NoChange : DeliveryReason::PartialOrUnexpected;
}
GrantAttempt apply_once(const GrantAccess& access, const AwardPacket& packet) {
    GrantAttempt result{};
    if (!valid_shared_packet(packet) || !access.snapshot || !access.grant ||
        !access.snapshot(access.context, packet, result.before))
        return result;
    if (!access.grant(access.context, packet))
        return result;
    result.attempted = true;
    result.reason = access.snapshot(access.context, packet, result.after)
                        ? verify_inventory_delta(packet, result.before, result.after)
                        : DeliveryReason::VerificationUnavailable;
    return result;
}
} // namespace shared_loot

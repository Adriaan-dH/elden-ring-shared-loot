#include "shop_purchase.h"
#include <algorithm>
#include <limits>
namespace shared_loot {
bool read_shop_offer(const MemoryReader& reader, std::uintptr_t context, AwardPacket& packet) {
    if (!reader.read || !context || context > UINTPTR_MAX - 0x84)
        return false;
    std::uint32_t item = 0, quantity = 0;
    std::uintptr_t row = 0;
    if (!reader.read(reader.context, context + 0x4c, &item, 4) ||
        !reader.read(reader.context, context + 0x60, &row, 8) || !row || row > UINTPTR_MAX - 0x1c ||
        !reader.read(reader.context, context + 0x80, &quantity, 4))
        return false;
    std::uint16_t bundle = 0;
    unsigned char type = 0;
    if (!reader.read(reader.context, row + 0x1a, &bundle, 2) ||
        !reader.read(reader.context, row + 0x17, &type, 1) || !quantity)
        return false;
    auto total = static_cast<std::uint64_t>(quantity) * std::max(1u, static_cast<unsigned>(bundle));
    if (total > std::numeric_limits<std::uint32_t>::max())
        return false;
    AwardPacket offer{};
    offer.count = 1;
    offer.entries[0] = {item, static_cast<std::uint32_t>(total), 0xffffffff, 0xffffffff};
    if (type == 5) {
        std::uint32_t gem = 0xffffffff;
        if (!reader.read(reader.context, context + 0x7c, &gem, 4))
            return false;
        if (gem != 0xffffffff) {
            if ((gem >> 28) != 8 || (gem & 0x0fffffff) == 0)
                return false;
            offer.entries[0].gem = gem & 0x0fffffff;
        }
    }
    if (!valid_shared_packet(offer))
        return false;
    packet = offer;
    return true;
}
ShopResult observe_purchase(const ShopAccess& access, const AwardPacket& offer) {
    ShopResult result{};
    result.packet = offer;
    if (!access.purchase)
        return result;
    InventoryCounts before{}, after{};
    std::uint16_t committed_before = 0, committed_after = 0;
    bool readable = valid_shared_packet(offer) && access.snapshot &&
                    access.snapshot(access.context, offer, before, committed_before);
    result.native_result = access.purchase(access.context); // exactly one native purchase
    if (!readable || !access.snapshot(access.context, offer, after, committed_after))
        return result;
    if (static_cast<std::uint16_t>(committed_after - committed_before) != 1) {
        result.outcome = ShopOutcome::NotCommitted;
        return result;
    }
    if (after[0] == before[0]) {
        result.outcome = ShopOutcome::NoChange;
        return result;
    }
    if (after[0] < before[0] || after[0] - before[0] > offer.entries[0].quantity) {
        result.outcome = ShopOutcome::UnexpectedDelta;
        return result;
    }
    result.packet.entries[0].quantity = static_cast<std::uint32_t>(after[0] - before[0]);
    result.outcome = ShopOutcome::Shared;
    return result;
}
const char* shop_outcome_name(ShopOutcome outcome) {
    switch (outcome) {
    case ShopOutcome::Shared:
        return "committed_inventory_delta";
    case ShopOutcome::Unavailable:
        return "purchase_probe_unavailable";
    case ShopOutcome::NotCommitted:
        return "purchase_not_committed";
    case ShopOutcome::NoChange:
        return "purchase_no_inventory_change";
    case ShopOutcome::UnexpectedDelta:
        return "purchase_unexpected_inventory_delta";
    }
    return "unknown";
}
} // namespace shared_loot

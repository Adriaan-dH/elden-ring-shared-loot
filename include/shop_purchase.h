#pragma once
#include "inventory_probe.h"
namespace shared_loot {
// Read-only fields of the guarded purchase transaction context. The native
// transaction still owns all charging, stock changes, and inventory mutation.
bool read_shop_offer(const MemoryReader&, std::uintptr_t context, AwardPacket&);
enum class ShopOutcome { Shared, Unavailable, NotCommitted, NoChange, UnexpectedDelta };
struct ShopResult {
    std::uintptr_t native_result = 0;
    ShopOutcome outcome = ShopOutcome::Unavailable;
    AwardPacket packet{};
};
struct ShopAccess {
    void* context;
    bool (*snapshot)(void*, const AwardPacket&, InventoryCounts&, std::uint16_t& committed);
    std::uintptr_t (*purchase)(void*);
};
// Capture only a committed purchase and its actual positive inventory delta.
// This never calls the purchase twice, retries payment, or infers purchases
// merely because an inventory changed while a shop was open.
ShopResult observe_purchase(const ShopAccess&, const AwardPacket&);
const char* shop_outcome_name(ShopOutcome);
} // namespace shared_loot

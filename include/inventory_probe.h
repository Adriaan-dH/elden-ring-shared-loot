#pragma once
#include "session_router.h"
#include <array>
#include <cstddef>
#include <cstdint>
namespace shared_loot {
struct MemoryReader {
    void* context;
    bool (*read)(void*, std::uintptr_t, void*, std::size_t);
};
using InventoryCounts = std::array<std::uint64_t, 10>;
// Read the active normal/key lists, not both inactive and active key copies.
// Counts accumulate so inventory and storage can be combined without writes.
bool inventory_counts(const MemoryReader&, std::uintptr_t inventory, const AwardPacket&,
                      InventoryCounts&);
DeliveryReason verify_inventory_delta(const AwardPacket&, const InventoryCounts& before,
                                      const InventoryCounts& after);
struct GrantAccess {
    void* context;
    bool (*snapshot)(void*, const AwardPacket&, InventoryCounts&);
    bool (*grant)(void*, const AwardPacket&);
};
struct GrantAttempt {
    bool attempted = false;
    DeliveryReason reason = DeliveryReason::InventoryUnavailable;
    InventoryCounts before{}, after{};
};
// A failed preflight may wait; once the native grant was called, its result is
// terminal for this delivery, including zero/partial/unreadable outcomes.
GrantAttempt apply_once(const GrantAccess&, const AwardPacket&);
} // namespace shared_loot

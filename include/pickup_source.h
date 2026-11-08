#pragma once
#include "inventory_probe.h"
#include "source_filter.h"
#include <cstring>
#include <limits>
namespace shared_loot {
// This is an acquisition-path classification, not a guess based on item ID.
// Treasure/enemy/gathering subtypes are deliberately not invented from a bag handle.
enum class PickupSource { NotWorldRecord, Unavailable, WorldPickup, PlayerLeft };
struct PickupEvidence {
    PickupSource source = PickupSource::NotWorldRecord;
    std::uint32_t object_handle = 0;
    std::uint16_t record_flags = 0;
};
inline const char* pickup_source_name(PickupSource source) {
    switch (source) {
    case PickupSource::Unavailable:
        return "world_source_unavailable";
    case PickupSource::WorldPickup:
        return "world_pickup";
    case PickupSource::PlayerLeft:
        return "player_left_marker";
    default:
        return "other_award_path";
    }
}
// Exact-build world queue layout. The packet lives inside its source record.
// Read BEFORE calling the native award: that call may consume the object.
// No address/handle is retained as an identity, and no coordinates are read.
inline PickupEvidence read_pickup_source(const MemoryReader& memory, bool game,
                                         std::uintptr_t caller_rva, std::uintptr_t packet,
                                         std::uintptr_t notification) {
    PickupEvidence result{};
    if (!game || caller_rva != 0x55ce2e)
        return result;
    result.source = PickupSource::Unavailable;
    if (!memory.read || packet < 0x40 ||
        packet > std::numeric_limits<std::uintptr_t>::max() - 0xa4 || notification != packet + 0xa4)
        return result;
    // Only the fixed prefix is copied. In particular we do not inspect the
    // optional creator context (which must not be mistaken for a Steam ID).
    unsigned char prefix[0x40]{};
    if (!memory.read(memory.context, packet - sizeof(prefix), prefix, sizeof(prefix)))
        return result;
    std::memcpy(&result.object_handle, prefix, sizeof(result.object_handle));
    std::memcpy(&result.record_flags, prefix + 0x30, sizeof(result.record_flags));
    // The native inventory Leave path sets bit 0x100 through the drop builder;
    // its record-copy path preserves it. Award-overflow bags pass zero instead.
    // This evidence belongs to the object, regardless of who later collects it.
    // Its meaning and propagation by Seamless still require a live two-PC test.
    result.source =
        (result.record_flags & 0x100) ? PickupSource::PlayerLeft : PickupSource::WorldPickup;
    return result;
}
inline bool pickup_source_allows_share(const PickupEvidence& evidence) {
    // An unreadable world record is not safe to broadcast. The game's own
    // pickup still proceeds normally; only the extra Shared Loot grant is blocked.
    return evidence.source == PickupSource::NotWorldRecord ||
           evidence.source == PickupSource::WorldPickup;
}
// Keep the acquisition allowlist and player-left exclusion in one decision so
// diagnostic eligibility and the actual send path cannot disagree.
inline bool shareable_pickup_source(bool game, bool seamless, std::uintptr_t rva,
                                    std::uint32_t flags, const AwardPacket& packet,
                                    const PickupEvidence& evidence) {
    if (rva == 0x55ce2e && evidence.source != PickupSource::WorldPickup)
        return false;
    return pickup_source_allows_share(evidence) &&
           broad_pickup_source(game, seamless, rva, flags, packet);
}
} // namespace shared_loot

#pragma once
#include <cstdint>
#include "award_packet.h"
#include "session_router.h"
namespace shared_loot {
enum class SourceHint {
    Unknown,
    WorldCandidate,
    AmbiguousGameAward,
    ExcludedScripted,
    ExcludedMod
};
inline SourceHint source_hint(bool game, bool seamless, std::uintptr_t rva, std::uint32_t flags) {
    if (seamless)
        return SourceHint::ExcludedMod;
    if (!game)
        return SourceHint::Unknown;
    if (rva == 0x56441c && flags == 4)
        return SourceHint::ExcludedScripted;
    if (rva == 0x55ce2e && flags <= 3)
        return SourceHint::WorldCandidate;
    if (rva == 0x56441c)
        return SourceHint::AmbiguousGameAward;
    return SourceHint::Unknown;
}
inline const char* hint_name(SourceHint hint) {
    switch (hint) {
    case SourceHint::WorldCandidate:
        return "ground_enemy_candidate";
    case SourceHint::AmbiguousGameAward:
        return "ambiguous_game_award";
    case SourceHint::ExcludedScripted:
        return "excluded_scripted_reward";
    case SourceHint::ExcludedMod:
        return "excluded_mod_grant";
    default:
        return "unknown";
    }
}
// Legacy diagnostic helper; the active Steam path uses broad_pickup_source below.
inline bool production_share_allowed(SourceHint) {
    return false;
}
// Explicit diagnostic only: the user arms one extra Rowa Fruit grant.
inline bool rowa_test_trigger(bool game, std::uintptr_t rva, std::uint32_t flags,
                              std::uint32_t count, std::uint32_t item, std::uint32_t quantity) {
    return game && rva == 0x56441c && flags == 0 && count == 1 && item == 0x400050f0 &&
           quantity > 0 && quantity <= 999;
}
// Service pending test items only during another ordinary observed acquisition.
// This is not a periodic tick and must never retain manager/context pointers.
inline bool rowa_delivery_callback(bool game, std::uintptr_t rva, std::uint32_t flags) {
    return game && flags <= 3 && (rva == 0x55ce2e || (rva == 0x56441c && flags == 0));
}
// Compatibility compromise: this allowlist identifies vanilla resources by
// resolved item ID. Randomized replacements can fall outside it; a script reward
// randomized into it can be included. It is not a complete source classifier.
inline bool known_gathering_item(std::uint32_t item) {
    if ((item >> 28) != 4)
        return false;
    auto id = item & 0x0fffffff;
    // Ruin Fragment uses the observed gathering path but is outside the normal
    // resource band. Confirmed by both PCs' first live Steam test logs.
    return (id >= 20500 && id <= 20999) || id == 1760;
}
// Runtime sharing decision, separate from the older diagnostic hint labels.
// Caller RVAs/flags are observations for the exact guarded game build only.
inline bool broad_pickup_source(bool game, bool seamless, std::uintptr_t rva, std::uint32_t flags,
                                const AwardPacket& packet) {
    if (!game || seamless || !valid_shared_packet(packet))
        return false;
    // Ground/corpse/chest/enemy loot path observed in the live run.
    if (rva == 0x55ce2e && flags <= 3) {
        for (unsigned i = 0; i < packet.count; ++i)
            if ((packet.entries[i].item >> 28) == 4 &&
                (packet.entries[i].item & 0x0fffffff) >= 8000000)
                return false;
        return true;
    }
    // General item-lot calls also carry NPC/scarab rewards. Permit only the
    // vanilla gathering-resource band here, never arbitrary scripted rewards.
    if (rva != 0x56441c || flags != 0)
        return false;
    for (unsigned i = 0; i < packet.count; ++i)
        if (!known_gathering_item(packet.entries[i].item))
            return false;
    return true;
}
} // namespace shared_loot

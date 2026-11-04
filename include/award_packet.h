#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace shared_loot {
// Observed in the installed 2.7.2.0 executable: a count, followed by up to
// ten 16-byte entries. Unknown fields are recorded, never interpreted/written.
struct AwardEntry {
    std::uint32_t item, quantity, unknown, gem;
};
struct AwardPacket {
    std::uint32_t count;
    AwardEntry entries[10];
};
static_assert(sizeof(AwardEntry) == 16);
static_assert(sizeof(AwardPacket) == 164);
inline bool decode_award_packet(const void* bytes, std::size_t length, AwardPacket& out) {
    out = {};
    if (!bytes || length < 4)
        return false;
    std::uint32_t count = 0;
    std::memcpy(&count, bytes, 4);
    if (count > 10 || length < 4 + count * sizeof(AwardEntry))
        return false;
    out.count = count;
    std::memcpy(out.entries, static_cast<const unsigned char*>(bytes) + 4,
                count * sizeof(AwardEntry));
    return true;
}
} // namespace shared_loot

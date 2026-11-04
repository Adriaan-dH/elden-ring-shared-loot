#include "award_packet.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                              \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
int main() {
    using namespace shared_loot;
    AwardPacket packet{};
    CHECK(!decode_award_packet(nullptr, 164, packet));
    unsigned char truncated[3]{};
    CHECK(!decode_award_packet(truncated, 3, packet));
    AwardPacket source{};
    source.count = 1;
    source.entries[0] = {0x400003e8, 3, 7, 0xffffffff};
    CHECK(!decode_award_packet(&source, 19, packet));
    CHECK(decode_award_packet(&source, 20, packet));
    CHECK(packet.count == 1 && packet.entries[0].quantity == 3);
    CHECK(packet.entries[0].item == 0x400003e8 && packet.entries[0].gem == 0xffffffff);
    source.count = 11;
    CHECK(!decode_award_packet(&source, sizeof(source), packet));
    source.count = 10;
    CHECK(decode_award_packet(&source, sizeof(source), packet));
    CHECK(packet.count == 10);
    source.count = 0;
    CHECK(decode_award_packet(&source, 4, packet));
    CHECK(packet.count == 0 && packet.entries[0].item == 0);
    std::puts("PASS: award packet bounds, metadata preservation, zero/maximum counts");
}

#include "source_filter.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                              \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
int main() {
    using namespace shared_loot;
    CHECK(source_hint(true, false, 0x55ce2e, 2) == SourceHint::WorldCandidate);
    CHECK(source_hint(true, false, 0x56441c, 0) == SourceHint::AmbiguousGameAward);
    CHECK(source_hint(true, false, 0x56441c, 4) == SourceHint::ExcludedScripted);
    CHECK(source_hint(false, true, 0x23fb0, 0) == SourceHint::ExcludedMod);
    CHECK(source_hint(false, false, 0x55ce2e, 2) == SourceHint::Unknown);
    for (auto hint :
         {SourceHint::Unknown, SourceHint::WorldCandidate, SourceHint::AmbiguousGameAward,
          SourceHint::ExcludedScripted, SourceHint::ExcludedMod})
        CHECK(!production_share_allowed(hint));
    CHECK(rowa_test_trigger(true, 0x56441c, 0, 1, 0x400050f0, 3));
    CHECK(!rowa_test_trigger(true, 0x56441c, 4, 1, 0x400050f0, 1));
    CHECK(!rowa_test_trigger(true, 0x56441c, 0, 1, 0x40001fde, 1));
    CHECK(!rowa_test_trigger(true, 0x56441c, 0, 1, 0x40038a40, 1));
    CHECK(!rowa_test_trigger(true, 0x55ce2e, 2, 1, 0x400006c2, 1));
    CHECK(!rowa_test_trigger(false, 0x56441c, 0, 1, 0x400050f0, 1));
    CHECK(!rowa_test_trigger(true, 0x56441c, 0, 2, 0x400050f0, 1));
    CHECK(!rowa_test_trigger(true, 0x56441c, 0, 1, 0x400050f0, 0));
    CHECK(rowa_delivery_callback(true, 0x56441c, 0));
    CHECK(rowa_delivery_callback(true, 0x55ce2e, 2));
    CHECK(!rowa_delivery_callback(true, 0x56441c, 4));
    CHECK(!rowa_delivery_callback(false, 0x55ce2e, 0));
    std::puts("PASS: observed-source exclusions, ambiguous paths blocked, diagnostic "
              "item/quantity/source allowlist");
}

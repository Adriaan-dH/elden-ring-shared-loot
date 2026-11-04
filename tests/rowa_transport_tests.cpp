#include "rowa_transport.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                   \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
using namespace shared_loot;
static std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}
static RowaConfig config(bool host, unsigned port) {
    RowaConfig c{};
    c.host = host;
    std::strcpy(c.address, "127.0.0.1");
    c.port = static_cast<std::uint16_t>(port);
    c.token.fill(0x53);
    c.nonce = host ? 101 : 202;
    return c;
}
template <class F>
static void until(RowaTransport& a, RowaTransport& b, F done, unsigned timeout = 4000) {
    auto end = now_ms() + timeout;
    while (now_ms() < end) {
        auto now = now_ms();
        a.tick(now);
        b.tick(now);
        if (done())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(false);
}
int main(int argc, char** argv) {
    unsigned port = argc > 2 ? static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10))
                             : 30000 + static_cast<unsigned>(now_ms() % 20000);
    if (argc > 1 && (std::strcmp(argv[1], "host") == 0 || std::strcmp(argv[1], "client") == 0)) {
        bool host = std::strcmp(argv[1], "host") == 0;
        RowaTransport peer;
        CHECK(peer.start(config(host, port)));
        peer.enable(true);
        bool sent = false, received = false;
        auto deadline = now_ms() + 8000, finish = deadline;
        while (now_ms() < deadline) {
            auto now = now_ms();
            peer.tick(now);
            if (!sent && peer.status().state == 4) {
                CHECK(peer.send_rowa(host ? 3 : 2));
                sent = true;
            }
            std::uint32_t quantity = 0;
            if (peer.take_rowa(quantity, now)) {
                CHECK(!received);
                CHECK(quantity == (host ? 2u : 3u));
                received = true;
                finish = now + 300;
            }
            if (sent && received && now >= finish) {
                std::puts("PASS: separate native TCP peer exchanged Rowa quantity");
                return 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(false);
    }
    RowaTransport host, client;
    CHECK(host.start(config(true, port)));
    CHECK(client.start(config(false, port)));
    until(host, client, [&] { return host.status().state == 3 && client.status().state == 3; });
    CHECK(!host.send_rowa(1));
    host.enable(true);
    client.enable(true);
    until(host, client, [&] { return host.status().state == 4 && client.status().state == 4; });
    CHECK(!host.send_rowa(0));
    CHECK(!host.send_rowa(1000));
    CHECK(host.send_rowa(3));
    until(host, client, [&] { return client.status().pending == 1; });
    std::uint32_t quantity = 0;
    CHECK(client.take_rowa(quantity, now_ms()) && quantity == 3);
    CHECK(!host.take_rowa(quantity, now_ms()));
    CHECK(client.send_rowa(2));
    until(host, client, [&] { return host.status().pending == 1; });
    CHECK(host.take_rowa(quantity, now_ms()) && quantity == 2);
    CHECK(host.send_rowa(1));
    until(host, client, [&] { return client.status().pending == 1; });
    CHECK(!client.take_rowa(quantity, now_ms() + 60001));
    CHECK(client.status().dropped == 1);
    CHECK(host.send_rowa(4));
    until(host, client, [&] { return client.status().pending == 1; });
    client.enable(false);
    CHECK(!client.take_rowa(quantity, now_ms()));
    CHECK(client.status().pending == 0);
    until(host, client, [&] { return host.status().state == 3 && client.status().state == 3; });
    client.enable(true);
    until(host, client, [&] { return host.status().state == 4 && client.status().state == 4; });
    CHECK(!client.take_rowa(quantity, now_ms()));
    host.stop();
    client.stop();
    ++port;
    auto wrong = config(false, port);
    wrong.token[0] ^= 1;
    CHECK(host.start(config(true, port)));
    CHECK(client.start(wrong));
    host.enable(true);
    client.enable(true);
    auto end = now_ms() + 200;
    while (now_ms() < end) {
        host.tick(now_ms());
        client.tick(now_ms());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(host.status().state != 4 && client.status().state != 4);
    CHECK(!host.send_rowa(1));
    std::puts("PASS: native transport handshake, bidirectional quantities, no echo, enable gate, "
              "expiry, disconnect discard, no replay, wrong token rejection");
}

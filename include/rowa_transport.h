#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace shared_loot {
struct RowaConfig {
    bool host = false;
    char address[64]{};
    std::uint16_t port = 49231;
    std::array<unsigned char, 32> token{};
    std::uint64_t nonce = 0;
};
struct RowaStatus {
    int state = 0; // 0 stopped, 1 waiting/connecting, 2 authenticating, 3 connected, 4 both enabled
    std::uint64_t sent = 0, received = 0, dropped = 0;
    unsigned pending = 0;
};
// One worker owns socket polling; game-thread calls use bounded protected queues.
// This LAN test is not a Steam-session API. Both users explicitly enable it.
class RowaTransport {
  public:
    ~RowaTransport();
    bool start(const RowaConfig& config);
    void stop();
    void tick(std::uint64_t now);
    void enable(bool value);
    bool send_rowa(std::uint32_t quantity);
    bool take_rowa(std::uint32_t& quantity, std::uint64_t now);
    RowaStatus status();

  private:
    void disconnect();
    void connected(std::uint64_t now);
    bool frame(unsigned kind, std::uint32_t quantity, std::uint64_t sequence = 0);
    bool parse(std::uint64_t now);
    std::mutex mutex_;
    RowaConfig config_{};
    std::intptr_t listener_ = -1, socket_ = -1;
    bool started_ = false, connecting_ = false, authenticated_ = false;
    bool enabled_ = false, remote_enabled_ = false, wsa_ = false;
    std::uint64_t remote_nonce_ = 0, next_sequence_ = 1, last_sequence_ = 0;
    std::uint64_t retry_at_ = 0, last_receive_ = 0, last_ping_ = 0;
    std::array<unsigned char, 8192> tx_{};
    std::size_t tx_size_ = 0, rx_size_ = 0;
    std::array<unsigned char, 64> rx_{};
    struct Pending {
        std::uint32_t quantity = 0;
        std::uint64_t time = 0;
    };
    std::array<Pending, 128> pending_{};
    unsigned pending_start_ = 0, pending_count_ = 0;
    RowaStatus stats_{};
};
} // namespace shared_loot

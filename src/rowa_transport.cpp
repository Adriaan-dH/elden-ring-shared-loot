// Legacy authenticated LAN diagnostic transport for Rowa Fruit tests.
// The release disables it and uses dynamic Steam membership/messaging instead.
#include "rowa_transport.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif
#include <algorithm>
#include <cstring>

namespace shared_loot {
namespace {
#ifdef _WIN32
using Native = SOCKET;
int error_code() {
    return WSAGetLastError();
}
bool waiting(int e) {
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
void close_socket(std::intptr_t s) {
    if (s != -1)
        closesocket(static_cast<Native>(s));
}
void nonblocking(std::intptr_t s) {
    u_long value = 1;
    ioctlsocket(static_cast<Native>(s), FIONBIO, &value);
}
#else
using Native = int;
int error_code() {
    return errno;
}
bool waiting(int e) {
    return e == EAGAIN || e == EWOULDBLOCK || e == EINPROGRESS;
}
void close_socket(std::intptr_t s) {
    if (s != -1)
        close(static_cast<Native>(s));
}
void nonblocking(std::intptr_t s) {
    fcntl(static_cast<Native>(s), F_SETFL, fcntl(static_cast<Native>(s), F_GETFL, 0) | O_NONBLOCK);
}
#endif
void put64(unsigned char* out, std::uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        out[i] = static_cast<unsigned char>(value);
        value >>= 8;
    }
}
std::uint64_t get64(const unsigned char* in) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
        value = (value << 8) | in[i];
    return value;
}
void put32(unsigned char* out, std::uint32_t value) {
    for (int i = 3; i >= 0; --i) {
        out[i] = static_cast<unsigned char>(value);
        value >>= 8;
    }
}
std::uint32_t get32(const unsigned char* in) {
    return (static_cast<std::uint32_t>(in[0]) << 24) | (static_cast<std::uint32_t>(in[1]) << 16) |
           (static_cast<std::uint32_t>(in[2]) << 8) | in[3];
}
} // namespace
RowaTransport::~RowaTransport() {
    stop();
}
bool RowaTransport::start(const RowaConfig& config) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (started_ || !config.nonce || !config.port || !config.address[0])
        return false;
    bool has_token = false;
    for (auto b : config.token)
        has_token = has_token || b != 0;
    if (!has_token)
        return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config.port);
    if (inet_pton(AF_INET, config.address, &address.sin_addr) != 1)
        return false;
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data))
        return false;
    wsa_ = true;
#endif
    config_ = config;
    if (config.host) {
        listener_ = static_cast<std::intptr_t>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (listener_ == -1)
            return false;
#ifndef _WIN32
        int reuse = 1;
        setsockopt(static_cast<Native>(listener_), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
        // Do not reuse an existing listener: a port conflict must fail visibly.
        if (bind(static_cast<Native>(listener_), reinterpret_cast<sockaddr*>(&address),
                 sizeof(address)) ||
            listen(static_cast<Native>(listener_), 1)) {
            close_socket(listener_);
            listener_ = -1;
            return false;
        }
        nonblocking(listener_);
    }
    started_ = true;
    stats_.state = 1;
    return true;
}
void RowaTransport::disconnect() {
    close_socket(socket_);
    socket_ = -1;
    connecting_ = false;
    authenticated_ = false;
    remote_enabled_ = false;
    remote_nonce_ = 0;
    last_sequence_ = 0;
    next_sequence_ = 1;
    stats_.dropped += pending_count_;
    pending_count_ = 0;
    pending_start_ = 0;
    tx_size_ = 0;
    rx_size_ = 0;
    stats_.state = started_ ? 1 : 0;
}
void RowaTransport::stop() {
    std::lock_guard<std::mutex> guard(mutex_);
    started_ = false;
    disconnect();
    close_socket(listener_);
    listener_ = -1;
    enabled_ = false;
#ifdef _WIN32
    if (wsa_) {
        WSACleanup();
        wsa_ = false;
    }
#endif
}
bool RowaTransport::frame(unsigned kind, std::uint32_t quantity, std::uint64_t sequence) {
    if (tx_size_ + 64 > tx_.size()) {
        ++stats_.dropped;
        return false;
    }
    auto* p = tx_.data() + tx_size_;
    std::memset(p, 0, 64);
    std::memcpy(p, "SLR3", 4);
    p[4] = static_cast<unsigned char>(kind);
    put64(p + 8, config_.nonce);
    put64(p + 16, sequence);
    put32(p + 24, quantity);
    std::memcpy(p + 32, config_.token.data(), 32);
    tx_size_ += 64;
    return true;
}
void RowaTransport::connected(std::uint64_t now) {
    connecting_ = false;
    stats_.state = 2;
    last_receive_ = now;
    last_ping_ = now;
    frame(1, 0);
    frame(2, enabled_ ? 1 : 0);
}
bool RowaTransport::parse(std::uint64_t now) {
    auto* p = rx_.data();
    unsigned difference = 0;
    for (unsigned i = 0; i < 32; ++i)
        difference |= p[32 + i] ^ config_.token[i];
    if (std::memcmp(p, "SLR3", 4) || difference || p[5] || p[6] || p[7] || get32(p + 28))
        return false;
    auto nonce = get64(p + 8), sequence = get64(p + 16);
    auto quantity = get32(p + 24);
    if (!nonce || nonce == config_.nonce)
        return false;
    if (p[4] == 1) {
        if (authenticated_ || sequence || quantity)
            return false;
        remote_nonce_ = nonce;
        authenticated_ = true;
        stats_.state = 3;
        return true;
    }
    if (!authenticated_ || nonce != remote_nonce_)
        return false;
    if (p[4] == 2) {
        if (sequence || quantity > 1)
            return false;
        remote_enabled_ = quantity == 1;
        if (!remote_enabled_) {
            stats_.dropped += pending_count_;
            pending_count_ = 0;
            pending_start_ = 0;
        }
        return true;
    }
    if (p[4] == 4)
        return sequence == 0 && quantity == 0;
    if (p[4] != 3 || !sequence || quantity == 0 || quantity > 999)
        return false;
    if (sequence <= last_sequence_)
        return true; // duplicate within this live connection
    last_sequence_ = sequence;
    if (!enabled_ || !remote_enabled_) {
        ++stats_.dropped;
        return true;
    }
    if (pending_count_ == pending_.size()) {
        ++stats_.dropped;
        return true;
    }
    pending_[(pending_start_ + pending_count_++) % pending_.size()] = {quantity, now};
    ++stats_.received;
    return true;
}
void RowaTransport::tick(std::uint64_t now) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!started_)
        return;
    if (socket_ == -1) {
        if (config_.host) {
            socket_ = static_cast<std::intptr_t>(
                accept(static_cast<Native>(listener_), nullptr, nullptr));
            if (socket_ != -1) {
                nonblocking(socket_);
                connected(now);
            }
        } else if (now >= retry_at_) {
            retry_at_ = now + 2000;
            socket_ = static_cast<std::intptr_t>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
            if (socket_ != -1) {
                nonblocking(socket_);
                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_port = htons(config_.port);
                inet_pton(AF_INET, config_.address, &address.sin_addr);
                int result = connect(static_cast<Native>(socket_),
                                     reinterpret_cast<sockaddr*>(&address), sizeof(address));
                if (result == 0)
                    connected(now);
                else if (waiting(error_code())) {
                    connecting_ = true;
                    last_receive_ = now;
                } else
                    disconnect();
            }
        }
    }
    if (socket_ == -1)
        return;
    if (connecting_) {
        fd_set writable, failed;
        FD_ZERO(&writable);
        FD_ZERO(&failed);
        FD_SET(static_cast<Native>(socket_), &writable);
        FD_SET(static_cast<Native>(socket_), &failed);
        timeval timeout{};
        int selected = select(static_cast<int>(socket_ + 1), nullptr, &writable, &failed, &timeout);
        if (selected > 0) {
            int error = 0;
#ifdef _WIN32
            int length = sizeof(error);
#else
            socklen_t length = sizeof(error);
#endif
            if (getsockopt(static_cast<Native>(socket_), SOL_SOCKET, SO_ERROR,
                           reinterpret_cast<char*>(&error), &length) ||
                error || FD_ISSET(static_cast<Native>(socket_), &failed)) {
                disconnect();
                return;
            }
            connected(now);
        }
        if (connecting_) {
            if (now - last_receive_ > 5000)
                disconnect();
            return;
        }
    }
    if (tx_size_) {
#ifdef _WIN32
        constexpr int send_flags = 0;
#else
        constexpr int send_flags = MSG_NOSIGNAL;
#endif
        int sent = static_cast<int>(::send(static_cast<Native>(socket_),
                                           reinterpret_cast<const char*>(tx_.data()),
                                           static_cast<int>(tx_size_), send_flags));
        if (sent > 0) {
            tx_size_ -= sent;
            std::memmove(tx_.data(), tx_.data() + sent, tx_size_);
        } else if (sent == 0 || !waiting(error_code())) {
            disconnect();
            return;
        }
    }
    // Bound work per tick so a peer cannot monopolize the game's worker.
    for (unsigned i = 0; i < 32; ++i) {
        int count = static_cast<int>(recv(static_cast<Native>(socket_),
                                          reinterpret_cast<char*>(rx_.data() + rx_size_),
                                          static_cast<int>(64 - rx_size_), 0));
        if (count > 0) {
            rx_size_ += count;
            last_receive_ = now;
            if (rx_size_ == 64) {
                if (!parse(now)) {
                    disconnect();
                    return;
                }
                rx_size_ = 0;
            }
        } else {
            if (count == 0 || !waiting(error_code()))
                disconnect();
            break;
        }
    }
    if (socket_ == -1)
        return;
    if (now - last_receive_ > 15000) {
        disconnect();
        return;
    }
    if (now - last_ping_ > 3000) {
        frame(4, 0);
        last_ping_ = now;
    }
    stats_.state = authenticated_ ? (enabled_ && remote_enabled_ ? 4 : 3) : 2;
}
void RowaTransport::enable(bool value) {
    std::lock_guard<std::mutex> guard(mutex_);
    enabled_ = value;
    if (!value) {
        disconnect();
        return;
    }
    if (socket_ != -1 && !connecting_)
        frame(2, 1);
}
bool RowaTransport::send_rowa(std::uint32_t quantity) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!started_ || !authenticated_ || !enabled_ || !remote_enabled_ || !quantity ||
        quantity > 999)
        return false;
    if (!frame(3, quantity, next_sequence_))
        return false;
    ++next_sequence_;
    ++stats_.sent;
    return true;
}
bool RowaTransport::take_rowa(std::uint32_t& quantity, std::uint64_t now) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!authenticated_ || !enabled_ || !remote_enabled_)
        return false;
    while (pending_count_) {
        auto entry = pending_[pending_start_];
        pending_start_ = (pending_start_ + 1) % pending_.size();
        --pending_count_;
        if (now - entry.time > 60000) {
            ++stats_.dropped;
            continue;
        }
        quantity = entry.quantity;
        return true;
    }
    return false;
}
RowaStatus RowaTransport::status() {
    std::lock_guard<std::mutex> guard(mutex_);
    auto status = stats_;
    status.pending = pending_count_;
    return status;
}
} // namespace shared_loot

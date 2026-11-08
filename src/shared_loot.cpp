#include "shared_loot.h"
#include <array>
#include <cstddef>
#include <limits>
#include <new>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
void sl_start_diagnostics(HINSTANCE module);
#endif

namespace {
constexpr std::size_t capacity = 256;
template <class T> struct Queue {
    std::array<T, capacity> values{};
    std::size_t start = 0, count = 0;
    bool full() const {
        return count == capacity;
    }
    bool empty() const {
        return count == 0;
    }
    void push(const T& value) {
        values[(start + count++) % capacity] = value;
    }
    T& front() {
        return values[start];
    }
    void pop() {
        start = (start + 1) % capacity;
        --count;
    }
};
bool eligible(uint32_t source) {
    return source == SL_FLOOR || source == SL_ENEMY;
}
bool valid_item(uint32_t item, uint32_t quantity) {
    // Broad diagnostic bounds, not a verified game item allowlist.
    return item != 0 && quantity > 0 && quantity <= 999;
}
struct Peer {
    uint64_t id = 0, sequence = 0;
};
} // namespace

// Portable C API retained for focused queue/grant tests. The active in-game
// Steam path uses SessionRouter for multi-item packets instead.
struct SL_Context {
    explicit SL_Context(uint64_t peer) : local(peer) {}
    uint64_t local;
    uint64_t next = 1;
    bool granting = false;
    Queue<SL_Event> outgoing, incoming;
    std::array<Peer, 8> peers{};
};

uint32_t SL_ApiVersion(void) {
    return 1;
}
SL_Context* SL_Create(uint64_t peer) {
    return peer == 0 ? nullptr : new (std::nothrow) SL_Context(peer);
}
void SL_Destroy(SL_Context* c) {
    delete c;
}
int SL_Capture(SL_Context* c, uint32_t item, uint32_t quantity, uint32_t source) {
    if (!c)
        return SL_INVALID;
    if (c->granting || !eligible(source))
        return SL_IGNORED;
    if (!valid_item(item, quantity))
        return SL_INVALID;
    if (c->outgoing.full() || c->next == std::numeric_limits<uint64_t>::max())
        return SL_FULL;
    c->outgoing.push({c->local, c->next++, item, quantity, source, 0});
    return SL_OK;
}
int SL_NextOutgoing(SL_Context* c, SL_Event* output) {
    if (!c || !output)
        return SL_INVALID;
    if (c->outgoing.empty())
        return SL_EMPTY;
    *output = c->outgoing.front();
    c->outgoing.pop();
    return SL_OK;
}
int SL_Receive(SL_Context* c, const SL_Event* event) {
    if (!c || !event || !event->origin || !event->sequence || event->reserved ||
        !eligible(event->source) || !valid_item(event->item, event->quantity))
        return SL_INVALID;
    if (event->origin == c->local)
        return SL_IGNORED;
    Peer* slot = nullptr;
    for (auto& peer : c->peers) {
        if (peer.id == event->origin) {
            slot = &peer;
            break;
        }
        if (!peer.id && !slot)
            slot = &peer;
    }
    if (!slot)
        return SL_FULL;
    if (slot->id == event->origin && event->sequence <= slot->sequence)
        return SL_DUPLICATE;
    if (c->incoming.full())
        return SL_FULL;
    c->incoming.push(*event);
    *slot = {event->origin, event->sequence};
    return SL_OK;
}
int SL_ApplyNext(SL_Context* c, SL_GrantFn grant, void* user) {
    if (!c || !grant || c->granting)
        return SL_INVALID;
    if (c->incoming.empty())
        return SL_EMPTY;
    const auto event = c->incoming.front();
    struct Guard {
        bool& flag;
        explicit Guard(bool& value) : flag(value) {
            flag = true;
        }
        ~Guard() {
            flag = false;
        }
    } guard(c->granting);
    if (grant(user, event.item, event.quantity) != 1)
        return SL_GRANT_FAILED;
    c->incoming.pop();
    return SL_OK;
}

#ifdef _WIN32
// Keep loader-lock work minimal: initialization is deferred to a worker.
// Loading this DLL alone does not prove that the guarded hooks are ready.
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        sl_start_diagnostics(module);
    }
    return TRUE;
}
#endif

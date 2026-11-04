#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include "shared_loot.h"
#include "award_packet.h"
#include "source_filter.h"
#include "rowa_transport.h"
#include "steam_bridge.h"
#include "installed_game_profile.h"
#include "verified_task_profile.h"
#include "MinHook.h"
#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace {
using shared_loot::AwardPacket;
using AwardFn = std::uintptr_t (*)(void*, const void*, void*, std::uint32_t);
AwardFn original_award = nullptr;
// Snapshot only value data at the hook boundary. The worker must never retain
// pointers to temporary game award packets or notification contexts.
struct Record {
    ULONGLONG time;
    DWORD thread;
    std::uintptr_t caller, module;
    char module_name[96];
    std::uint32_t flags;
    bool valid;
    bool is_game;
    bool is_seamless;
    unsigned kind;
    AwardPacket packet;
};
// Logging is buffered so file I/O stays off the award hook/game frame thread.
// If the diagnostic queue is contended/full, drop records instead of blocking play.
SRWLOCK queue_lock = SRWLOCK_INIT;
Record records[512]{};
unsigned queue_start = 0, queue_count = 0;
volatile LONG64 dropped = 0;
volatile LONG starting = 0;
volatile LONG grant_enabled = 0;
// 0 idle, 1 armed, 2 consumed (at most one test attempt per process).
volatile LONG grant_state = 0;
volatile LONG64 grant_armed_at = 0;
shared_loot::RowaTransport network;
volatile LONG network_started = 0;
volatile LONG outbound_enabled = 1;
shared_loot::SteamBridge steam_bridge;
volatile LONG steam_started = 0, steam_paused = 0;
volatile LONG steam_gathering = 1;
volatile LONG64 frame_ticks = 0;
// Serialize native awards against incoming grants. The thread-local guard
// prevents our own grants from being mistaken for new player acquisitions.
SRWLOCK award_gate = SRWLOCK_INIT;
thread_local bool inside = false;

bool read_local(const void* pointer, void* target, SIZE_T length) {
    SIZE_T read = 0;
    return pointer && ReadProcessMemory(GetCurrentProcess(), pointer, target, length, &read) &&
           read == length;
}
bool snapshot(const void* pointer, AwardPacket& result) {
    std::uint32_t count = 0;
    if (!read_local(pointer, &count, 4) || count > 10)
        return false;
    unsigned char raw[sizeof(AwardPacket)]{};
    SIZE_T length = 4 + count * sizeof(shared_loot::AwardEntry);
    if (!read_local(pointer, raw, length))
        return false;
    return shared_loot::decode_award_packet(raw, length, result);
}
void push_record(const Record& record) {
    if (!TryAcquireSRWLockExclusive(&queue_lock)) {
        InterlockedIncrement64(&dropped);
        return;
    }
    if (queue_count == 512)
        InterlockedIncrement64(&dropped);
    else
        records[(queue_start + queue_count++) % 512] = record;
    ReleaseSRWLockExclusive(&queue_lock);
}
bool pop_record(Record& record) {
    AcquireSRWLockExclusive(&queue_lock);
    bool present = queue_count != 0;
    if (present) {
        record = records[queue_start];
        queue_start = (queue_start + 1) % 512;
        --queue_count;
    }
    ReleaseSRWLockExclusive(&queue_lock);
    return present;
}
// Legacy one-shot Rowa Fruit diagnostic; disabled in the release INI.
// Kept to reproduce the early local-grant experiments independently of Steam.
bool run_rowa_test(const Record& source, void* manager, const void* context) {
    if (!InterlockedCompareExchange(&grant_enabled, 0, 0) || !source.valid ||
        !shared_loot::rowa_test_trigger(source.is_game, source.caller - source.module, source.flags,
                                        source.packet.count, source.packet.entries[0].item,
                                        source.packet.entries[0].quantity) ||
        GetTickCount64() -
                static_cast<ULONGLONG>(InterlockedCompareExchange64(&grant_armed_at, 0, 0)) >
            120000 ||
        InterlockedCompareExchange(&grant_state, 2, 1) != 1)
        return false;
    // Static inspection shows this argument's first three words are read for
    // notification context. Copy them while the source call's context is alive.
    std::uint32_t notification[8]{};
    Record issued = source;
    issued.kind = 1;
    if (!read_local(context, notification, 12)) {
        issued.valid = false;
        push_record(issued);
        return true;
    }
    AwardPacket extra{};
    extra.count = 1;
    extra.entries[0] = source.packet.entries[0];
    extra.entries[0].quantity = 1;
    issued.packet = extra;
    // Call the original trampoline, not the detour, on the current game thread.
    // The outer recursion guard remains set throughout this diagnostic grant.
    original_award(manager, &extra, notification, 0);
    issued.time = GetTickCount64();
    push_record(issued);
    return true;
}
bool issue_remote_rowa(const Record& source, void* manager, void* notification,
                       std::uint32_t quantity) {
    if (!source.valid || !quantity || quantity > 999 ||
        !shared_loot::rowa_delivery_callback(source.is_game, source.caller - source.module,
                                             source.flags))
        return false;
    AwardPacket extra{};
    extra.count = 1;
    extra.entries[0] = {0x400050f0, quantity, 0xffffffff, 0xffffffff};
    original_award(manager, &extra, notification, 0);
    Record issued = source;
    issued.kind = 3;
    issued.packet = extra;
    issued.time = GetTickCount64();
    push_record(issued);
    return true;
}
void process_network_award(const Record& source, void* manager, const void* context) {
    if (!InterlockedCompareExchange(&network_started, 0, 0) || !source.valid)
        return;
    if (InterlockedCompareExchange(&outbound_enabled, 0, 0) &&
        shared_loot::rowa_test_trigger(source.is_game, source.caller - source.module, source.flags,
                                       source.packet.count, source.packet.entries[0].item,
                                       source.packet.entries[0].quantity) &&
        network.send_rowa(source.packet.entries[0].quantity)) {
        Record sent = source;
        sent.kind = 2;
        push_record(sent);
    }
    if (!shared_loot::rowa_delivery_callback(source.is_game, source.caller - source.module,
                                             source.flags))
        return;
    std::uint32_t notification[8]{};
    if (!read_local(context, notification, 12))
        return;
    for (unsigned i = 0; i < 8; ++i) {
        std::uint32_t quantity = 0;
        if (!network.take_rowa(quantity, GetTickCount64()))
            break;
        issue_remote_rowa(source, manager, notification, quantity);
    }
}
// Copy eligible resolved awards into the Steam router after the original
// acquisition completes. Source filtering is heuristic; see source_filter.h.
void capture_steam_award(const Record& source) {
    if (!InterlockedCompareExchange(&steam_started, 0, 0) ||
        InterlockedCompareExchange(&steam_paused, 0, 0) || !source.valid)
        return;
    if (source.caller - source.module == 0x56441c &&
        !InterlockedCompareExchange(&steam_gathering, 0, 0))
        return;
    if (shared_loot::broad_pickup_source(source.is_game, source.is_seamless,
                                         source.caller - source.module, source.flags,
                                         source.packet) &&
        steam_bridge.capture(source.packet, GetTickCount64())) {
        Record sent = source;
        sent.kind = 4;
        push_record(sent);
    }
}
// Detour the verified award function while preserving arguments/return value.
// Determine provenance from the caller module and executable-relative address.
std::uintptr_t observe_award(void* manager, const void* packet, void* output, std::uint32_t flags) {
    if (inside)
        return original_award(manager, packet, output, flags);
    struct Guard {
        Guard() {
            AcquireSRWLockExclusive(&award_gate);
            inside = true;
        }
        ~Guard() {
            inside = false;
            ReleaseSRWLockExclusive(&award_gate);
        }
    } guard;
    Record record{};
    record.time = GetTickCount64();
    record.thread = GetCurrentThreadId();
#ifdef _MSC_VER
    record.caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
#else
    record.caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
#endif
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<void*>(record.caller), &info, sizeof(info))) {
        record.module = reinterpret_cast<std::uintptr_t>(info.AllocationBase);
        wchar_t name[1024]{};
        if (GetModuleFileNameW(static_cast<HMODULE>(info.AllocationBase), name, 1024)) {
            const wchar_t* base = std::wcsrchr(name, L'\\');
            base = base ? base + 1 : name;
            unsigned i = 0;
            for (; base[i] && i < 95; ++i) {
                auto ch = base[i];
                record.module_name[i] =
                    ch >= 32 && ch < 127 && ch != L'"' && ch != L'\\' ? static_cast<char>(ch) : '_';
            }
        }
    }
    record.flags = flags;
    record.is_game = record.module == reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    record.is_seamless = _stricmp(record.module_name, "ersc.dll") == 0;
    record.valid = snapshot(packet, record.packet);
    push_record(record);
    auto result = original_award(manager, packet, output, flags);
    run_rowa_test(record, manager, output);
    process_network_award(record, manager, output);
    capture_steam_award(record);
    return result;
}

bool object_pointer(std::uintptr_t pointer, std::uintptr_t base) {
    std::uintptr_t vtable = 0;
    return pointer && read_local(reinterpret_cast<void*>(pointer), &vtable, 8) && vtable >= base &&
           vtable < base + shared_loot::profile::image_size;
}
bool issue_shared_packet(void* manager, const AwardPacket& packet) {
    if (!inside || !shared_loot::valid_shared_packet(packet))
        return false;
    std::uint32_t notification[8]{};
    original_award(manager, &packet, notification, 0);
    Record issued{};
    issued.kind = 5;
    issued.valid = true;
    issued.time = GetTickCount64();
    issued.thread = GetCurrentThreadId();
    issued.packet = packet;
    push_record(issued);
    return true;
}
// Incoming grants must run on a game task, never on the networking worker.
// Re-read managers/player each frame and wait for 30 stable frames after loads.
// Apply at most four bundles per frame to bound the work done on this thread.
void frame_execute(void*, const void*) {
    InterlockedIncrement64(&frame_ticks);
    if (!InterlockedCompareExchange(&steam_started, 0, 0))
        return;
    auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::uintptr_t manager = 0, world = 0, player = 0;
    bool ready =
        read_local(reinterpret_cast<void*>(base + shared_loot::profile::map_item_manager_rva),
                   &manager, 8) &&
        object_pointer(manager, base) &&
        read_local(reinterpret_cast<void*>(base + shared_loot::profile::world_chr_manager_rva),
                   &world, 8) &&
        object_pointer(world, base) &&
        read_local(reinterpret_cast<void*>(world + shared_loot::profile::local_player_offset),
                   &player, 8) &&
        object_pointer(player, base);
    static std::uintptr_t previous_player = 0;
    static unsigned stable = 0;
    if (!ready || player != previous_player) {
        stable = 0;
        steam_bridge.set_world(false);
    } else if (stable < 30)
        ++stable;
    previous_player = ready ? player : 0;
    ready = ready && stable >= 30 && !InterlockedCompareExchange(&steam_paused, 0, 0);
    steam_bridge.set_world(ready);
    if (!ready || inside || !TryAcquireSRWLockExclusive(&award_gate))
        return;
    inside = true;
    for (unsigned i = 0; i < 4; ++i) {
        shared_loot::Delivery delivery{};
        if (!steam_bridge.take(delivery, GetTickCount64()))
            break;
        if (!shared_loot::valid_shared_packet(delivery.packet))
            continue;
        issue_shared_packet(reinterpret_cast<void*>(manager), delivery.packet);
    }
    inside = false;
    ReleaseSRWLockExclusive(&award_gate);
}
void* task_runtime_class(void*) {
    return nullptr;
}
void task_destroy(void*) {}
// Minimal verified FD4 task ABI. Storage and vtable stay alive for the process;
// the game may invoke the task long after registration returns.
struct FrameTask {
    void** vtable;
    std::uint64_t unused[7]{};
};
void* frame_vtable[] = {reinterpret_cast<void*>(&task_runtime_class),
                        reinterpret_cast<void*>(&task_destroy),
                        reinterpret_cast<void*>(&frame_execute)};
FrameTask frame_task{frame_vtable, {}};
// Check the exact registration entry point before inserting our persistent
// task into WorldChrMan_PostPhysics (group 117). A mismatch disables delivery.
bool register_frame_task(FILE* log) {
    auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    unsigned char bytes[32]{};
    if (!read_local(reinterpret_cast<void*>(base + shared_loot::profile::register_task_rva), bytes,
                    32) ||
        std::memcmp(bytes, shared_loot::profile::register_task_prologue, 32)) {
        std::fputs(
            "{\"event\":\"frame_task_disabled\",\"reason\":\"register_signature_changed\"}\n", log);
        return false;
    }
    std::uintptr_t storage = 0;
    if (!read_local(reinterpret_cast<void*>(base + shared_loot::profile::task_storage_rva),
                    &storage, 8) ||
        !storage) {
        std::fputs("{\"event\":\"frame_task_disabled\",\"reason\":\"task_system_not_ready\"}\n",
                   log);
        return false;
    }
    using Register = bool (*)(void*, std::uint32_t, void*);
    auto call = reinterpret_cast<Register>(base + shared_loot::profile::register_task_rva);
    bool result = call(reinterpret_cast<void*>(storage), 117, &frame_task);
    std::fprintf(log, "{\"event\":\"frame_task_registered\",\"group\":117,\"result\":%s}\n",
                 result ? "true" : "false");
    return result;
}

bool hash_file(const wchar_t* path, char (&hex)[65]) {
    HANDLE file =
        CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
              BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    unsigned char buffer[65536], digest[32];
    DWORD length = 0;
    while (ok) {
        if (!ReadFile(file, buffer, sizeof(buffer), &length, nullptr)) {
            ok = false;
            break;
        }
        if (!length)
            break;
        ok = BCryptHashData(hash, buffer, length, 0) >= 0;
    }
    if (ok)
        ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash)
        BCryptDestroyHash(hash);
    if (algorithm)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (ok)
        for (unsigned i = 0; i < 32; ++i)
            std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return ok;
}
// Hooks depend on verified executable offsets, not merely a version string.
// Keep the executable hash and instruction guards even though release ZIPs do
// not include file-hash manifests: these checks prevent hooking another build.
bool profile_matches(HMODULE game, const wchar_t* path, FILE* log) {
    char digest[65]{};
    if (!hash_file(path, digest) || std::strcmp(digest, shared_loot::profile::sha256)) {
        std::fprintf(log,
                     "{\"event\":\"disabled\",\"reason\":\"executable_hash_mismatch\",\"actual_"
                     "sha256\":\"%s\"}\n",
                     digest);
        return false;
    }
    auto* base = reinterpret_cast<unsigned char*>(game);
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!read_local(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 0 || dos.e_lfanew > 0x100000 ||
        !read_local(base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.TimeDateStamp != shared_loot::profile::timestamp ||
        nt.OptionalHeader.SizeOfImage != shared_loot::profile::image_size) {
        std::fputs("{\"event\":\"disabled\",\"reason\":\"loaded_image_mismatch\"}\n", log);
        return false;
    }
    unsigned char prologue[32]{};
    if (!read_local(base + shared_loot::profile::award_rva, prologue, 32) ||
        std::memcmp(prologue, shared_loot::profile::prologue, 32)) {
        std::fputs(
            "{\"event\":\"disabled\",\"reason\":\"award_prologue_changed_or_already_hooked\"}\n",
            log);
        return false;
    }
    return true;
}
void flush_records(FILE* log) {
    Record record{};
    while (pop_record(record)) {
        auto hint = shared_loot::source_hint(record.is_game, record.is_seamless,
                                             record.caller - record.module, record.flags);
        bool eligible =
            record.valid && (record.kind == 4 ||
                             (record.kind == 0 &&
                              (record.caller - record.module != 0x56441c ||
                               InterlockedCompareExchange(&steam_gathering, 0, 0)) &&
                              shared_loot::broad_pickup_source(record.is_game, record.is_seamless,
                                                               record.caller - record.module,
                                                               record.flags, record.packet)));
        std::fprintf(log,
                     "{\"event\":\"%s\",\"tick_ms\":%llu,\"thread\":%lu,\"caller_module\":\"%s\","
                     "\"caller_rva\":\"0x%llx\",\"flags\":%u,\"packet_valid\":%s,\"source_hint\":"
                     "\"%s\",\"sharing_eligible\":%s,\"items\":[",
                     record.kind == 1   ? "grant_test_request"
                     : record.kind == 2 ? "rowa_sent"
                     : record.kind == 3 ? "rowa_remote_grant_request"
                     : record.kind == 4 ? "steam_pickup_sent"
                     : record.kind == 5 ? "steam_grant_request"
                                        : "award_request",
                     static_cast<unsigned long long>(record.time),
                     static_cast<unsigned long>(record.thread), record.module_name,
                     static_cast<unsigned long long>(record.caller - record.module), record.flags,
                     record.valid ? "true" : "false",
                     record.kind ? "addon_action" : shared_loot::hint_name(hint),
                     eligible ? "true" : "false");
        if (record.valid)
            for (unsigned i = 0; i < record.packet.count; ++i) {
                const auto& item = record.packet.entries[i];
                std::fprintf(
                    log,
                    "%s{\"raw_item\":\"0x%08x\",\"quantity\":%u,\"unknown\":%u,\"gem\":\"0x%08x\"}",
                    i ? "," : "", item.item, item.quantity, item.unknown, item.gem);
            }
        std::fputs("]}\n", log);
    }
    LONG64 lost = InterlockedExchange64(&dropped, 0);
    if (lost)
        std::fprintf(log, "{\"event\":\"dropped_diagnostics\",\"count\":%lld}\n",
                     static_cast<long long>(lost));
    std::fflush(log);
}
[[maybe_unused]] bool configure_network(const wchar_t* config, FILE* log) {
    if (GetPrivateProfileIntW(L"network", L"enabled", 0, config) != 1)
        return false;
    if (InterlockedCompareExchange(&grant_enabled, 0, 0) || !GetModuleHandleW(L"ersc.dll"))
        return false;
    wchar_t role[16]{}, address[64]{}, token[80]{};
    GetPrivateProfileStringW(L"network", L"role", L"", role, 16, config);
    GetPrivateProfileStringW(L"network", L"address", L"", address, 64, config);
    GetPrivateProfileStringW(L"network", L"token", L"", token, 80, config);
    unsigned port = GetPrivateProfileIntW(L"network", L"port", 49231, config);
    shared_loot::RowaConfig options{};
    bool valid = (std::wcscmp(role, L"host") == 0 || std::wcscmp(role, L"client") == 0) &&
                 std::wcslen(token) == 64 && port > 0 && port <= 65535;
    options.host = std::wcscmp(role, L"host") == 0;
    options.port = static_cast<std::uint16_t>(port);
    for (unsigned i = 0; i < 63 && address[i]; ++i) {
        if (address[i] > 127)
            valid = false;
        options.address[i] = static_cast<char>(address[i]);
    }
    auto hex = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9')
            return c - L'0';
        if (c >= L'a' && c <= L'f')
            return c - L'a' + 10;
        if (c >= L'A' && c <= L'F')
            return c - L'A' + 10;
        return -1;
    };
    if (valid)
        for (unsigned i = 0; i < 32; ++i) {
            int a = hex(token[2 * i]), b = hex(token[2 * i + 1]);
            if (a < 0 || b < 0) {
                valid = false;
                break;
            }
            options.token[i] = static_cast<unsigned char>((a << 4) | b);
        }
    valid = valid && BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&options.nonce),
                                     sizeof(options.nonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
    if (!valid || !network.start(options)) {
        std::fputs("{\"event\":\"network_disabled\",\"reason\":\"invalid_config_or_socket_start_"
                   "failed\"}\n",
                   log);
        return false;
    }
    InterlockedExchange(&outbound_enabled,
                        GetPrivateProfileIntW(L"network", L"send_pickups", 1, config) == 1 ? 1 : 0);
    InterlockedExchange(&network_started, 1);
    std::fprintf(log,
                 "{\"event\":\"network_available\",\"role\":\"%s\",\"port\":%u,\"send_pickups\":%s,"
                 "\"item\":\"Rowa Fruit "
                 "only\",\"enable_key\":\"F8\",\"disable_key\":\"F9\",\"toggle_send_key\":\"F10\","
                 "\"delivery\":\"next_local_acquisition\"}\n",
                 options.host ? "host" : "client", port,
                 InterlockedCompareExchange(&outbound_enabled, 0, 0) ? "true" : "false");
    return true;
}
// Initialize outside DllMain/loader lock, then poll Steam and flush logs.
// The historical diagnostics.enabled option currently gates the entire worker.
DWORD WINAPI diagnostic_worker(void* parameter) {
    HMODULE module = static_cast<HMODULE>(parameter), pinned = nullptr;
    // Once hooks exist the DLL and trampoline must remain loaded to process exit.
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&diagnostic_worker), &pinned))
        return 0;
    wchar_t path[32768]{};
    DWORD length = GetModuleFileNameW(module, path, 32768);
    if (!length || length >= 32768)
        return 0;
    wchar_t* slash = std::wcsrchr(path, L'\\');
    if (!slash)
        return 0;
    *slash = L'\0';
    wchar_t config[32768]{}, logs[32768]{}, logfile[32768]{};
    if (std::swprintf(config, 32768, L"%ls\\SharedLoot.ini", path) < 0 ||
        std::swprintf(logs, 32768, L"%ls\\logs", path) < 0)
        return 0;
    if (!CreateDirectoryW(logs, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return 0;
    SYSTEMTIME now{};
    GetSystemTime(&now);
    if (std::swprintf(logfile, 32768, L"%ls\\diagnostics-%04u%02u%02u-%02u%02u%02u-%lu.jsonl", logs,
                      now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
                      static_cast<unsigned long>(GetCurrentProcessId())) < 0)
        return 0;
    FILE* log = _wfopen(logfile, L"wb");
    if (!log)
        return 0;
    std::fputs("{\"event\":\"startup\",\"build\":\"0.4.1\",\"mode\":\"steam_shared_loot\",\"lan_"
               "bridge\":false}\n",
               log);
    if (GetPrivateProfileIntW(L"diagnostics", L"enabled", 0, config) != 1) {
        std::fputs("{\"event\":\"disabled\",\"reason\":\"config_missing_or_disabled\"}\n", log);
        std::fclose(log);
        return 0;
    }
    // The loader/profile must have disabled EAC; do not attach in an EAC process.
    if (GetModuleHandleW(L"EasyAntiCheat_EOS.dll") || GetModuleHandleW(L"EasyAntiCheat.dll")) {
        std::fputs("{\"event\":\"disabled\",\"reason\":\"eac_module_present\"}\n", log);
        std::fclose(log);
        return 0;
    }
    // Give separately loaded native mods time to initialize. This is not a
    // readiness guarantee: live co-existence is still part of the first test.
    Sleep(3000);
    HMODULE game = GetModuleHandleW(nullptr);
    wchar_t game_path[32768]{};
    GetModuleFileNameW(game, game_path, 32768);
    if (!profile_matches(game, game_path, log)) {
        std::fclose(log);
        return 0;
    }
    if (GetPrivateProfileIntW(L"grant_test", L"enabled", 0, config) == 1 &&
        GetModuleHandleW(L"ersc.dll")) {
        InterlockedExchange(&grant_enabled, 1);
        std::fputs("{\"event\":\"grant_test_available\",\"key\":\"F8\",\"item\":\"Rowa "
                   "Fruit\",\"extra_quantity\":1,\"limit_per_launch\":1,\"timeout_seconds\":120}\n",
                   log);
    }
    void* target = reinterpret_cast<unsigned char*>(game) + shared_loot::profile::award_rva;
    MH_STATUS status = MH_Initialize();
    if (status == MH_OK)
        status = MH_CreateHook(target, reinterpret_cast<void*>(&observe_award),
                               reinterpret_cast<void**>(&original_award));
    if (status == MH_OK)
        status = MH_EnableHook(target);
    std::fprintf(log, "{\"event\":\"hook_status\",\"status\":\"%s\",\"rva\":\"0x%x\"}\n",
                 MH_StatusToString(status), shared_loot::profile::award_rva);
    std::fflush(log);
    if (status != MH_OK) {
        std::fclose(log);
        return 0;
    }
    bool steam_requested = GetPrivateProfileIntW(L"steam", L"enabled", 1, config) == 1 &&
                           GetModuleHandleW(L"ersc.dll");
    InterlockedExchange(
        &steam_gathering,
        GetPrivateProfileIntW(L"sharing", L"gathering_materials", 1, config) == 1 ? 1 : 0);
    if (steam_requested) {
        if (steam_bridge.start()) {
            InterlockedExchange(&steam_started, 1);
            std::fprintf(log,
                         "{\"event\":\"steam_available\",\"channel\":%d,\"discovery\":\"lobby_"
                         "callbacks_and_membership\",\"roles\":\"symmetric\"}\n",
                         shared_loot::steam_channel);
            register_frame_task(log);
        } else
            std::fputs("{\"event\":\"steam_unavailable\",\"reason\":\"interfaces_not_ready_or_"
                       "missing\"}\n",
                       log);
    }
    bool previous_down = false, previous_stop = false, previous_toggle = false;
    shared_loot::RowaStatus last_status{};
    shared_loot::SteamStatus last_steam{};
    bool previous_frame_active = false;
    ULONGLONG next_steam_attempt = GetTickCount64() + 1000;
    for (;;) {
        if (steam_requested && !InterlockedCompareExchange(&steam_started, 0, 0) &&
            GetTickCount64() >= next_steam_attempt) {
            next_steam_attempt = GetTickCount64() + 1000;
            if (steam_bridge.start()) {
                InterlockedExchange(&steam_started, 1);
                std::fputs("{\"event\":\"steam_available\",\"late_initialization\":true}\n", log);
                register_frame_task(log);
            }
        }
        bool down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        bool stop = (GetAsyncKeyState(VK_F9) & 0x8000) != 0,
             toggle = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
        DWORD foreground_pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
        if (InterlockedCompareExchange(&steam_started, 0, 0)) {
            if (foreground_pid == GetCurrentProcessId()) {
                if (stop && !previous_stop) {
                    InterlockedExchange(&steam_paused, 1);
                    std::fputs("{\"event\":\"sharing_paused\"}\n", log);
                }
                if (down && !previous_down) {
                    InterlockedExchange(&steam_paused, 0);
                    std::fputs("{\"event\":\"sharing_resumed\"}\n", log);
                }
            }
            steam_bridge.tick(GetTickCount64());
            auto current = steam_bridge.status();
            bool active = InterlockedCompareExchange64(&frame_ticks, 0, 0) > 0;
            if (current.lobby != last_steam.lobby || current.world != last_steam.world ||
                current.router.members != last_steam.router.members ||
                current.router.ready != last_steam.router.ready ||
                current.router.pending != last_steam.router.pending ||
                current.router.sent != last_steam.router.sent ||
                current.router.received != last_steam.router.received ||
                current.router.rejected != last_steam.router.rejected ||
                current.send_errors != last_steam.send_errors ||
                current.callbacks != last_steam.callbacks || current.hellos != last_steam.hellos ||
                active != previous_frame_active) {
                std::fprintf(log,
                             "{\"event\":\"steam_status\",\"lobby\":%s,\"world_ready\":%s,"
                             "\"members\":%u,\"ready_peers\":%u,\"pending\":%u,\"sent\":%llu,"
                             "\"received\":%llu,\"rejected\":%llu,\"send_errors\":%llu,\"lobby_"
                             "callbacks\":%llu,\"hello_sent\":%llu,\"frame_active\":%s}\n",
                             current.lobby ? "true" : "false", current.world ? "true" : "false",
                             current.router.members, current.router.ready, current.router.pending,
                             static_cast<unsigned long long>(current.router.sent),
                             static_cast<unsigned long long>(current.router.received),
                             static_cast<unsigned long long>(current.router.rejected),
                             static_cast<unsigned long long>(current.send_errors),
                             static_cast<unsigned long long>(current.callbacks),
                             static_cast<unsigned long long>(current.hellos),
                             active ? "true" : "false");
                last_steam = current;
                previous_frame_active = active;
            }
        }
        if (InterlockedCompareExchange(&network_started, 0, 0)) {
            if (foreground_pid == GetCurrentProcessId()) {
                if (down && !previous_down) {
                    network.enable(true);
                    std::fputs("{\"event\":\"network_user_enabled\"}\n", log);
                }
                if (stop && !previous_stop) {
                    network.enable(false);
                    std::fputs("{\"event\":\"network_user_disabled\"}\n", log);
                }
                if (toggle && !previous_toggle) {
                    LONG current = InterlockedCompareExchange(&outbound_enabled, 0, 0);
                    InterlockedExchange(&outbound_enabled, current ? 0 : 1);
                    std::fprintf(log, "{\"event\":\"outbound_mode\",\"enabled\":%s}\n",
                                 current ? "false" : "true");
                }
            }
            network.tick(GetTickCount64());
            auto current = network.status();
            if (current.state != last_status.state || current.sent != last_status.sent ||
                current.received != last_status.received ||
                current.pending != last_status.pending || current.dropped != last_status.dropped) {
                std::fprintf(log,
                             "{\"event\":\"network_status\",\"state\":%d,\"sent\":%llu,"
                             "\"received\":%llu,\"pending\":%u,\"dropped\":%llu}\n",
                             current.state, static_cast<unsigned long long>(current.sent),
                             static_cast<unsigned long long>(current.received), current.pending,
                             static_cast<unsigned long long>(current.dropped));
                last_status = current;
            }
        }
        if (InterlockedCompareExchange(&grant_enabled, 0, 0) && down && !previous_down &&
            foreground_pid == GetCurrentProcessId() &&
            InterlockedCompareExchange(&grant_state, 0, 0) == 0) {
            InterlockedExchange64(&grant_armed_at, static_cast<LONG64>(GetTickCount64()));
            if (InterlockedCompareExchange(&grant_state, 1, 0) == 0)
                std::fputs("{\"event\":\"grant_test_armed\",\"timeout_seconds\":120}\n", log);
        }
        previous_down = down;
        previous_stop = stop;
        previous_toggle = toggle;
        if (InterlockedCompareExchange(&grant_state, 0, 0) == 1 &&
            GetTickCount64() -
                    static_cast<ULONGLONG>(InterlockedCompareExchange64(&grant_armed_at, 0, 0)) >
                120000 &&
            InterlockedCompareExchange(&grant_state, 0, 1) == 1)
            std::fputs("{\"event\":\"grant_test_expired\"}\n", log);
        flush_records(log);
        Sleep(50);
    }
}
} // namespace

void sl_start_diagnostics(HINSTANCE module) {
    wchar_t process[32768]{};
    DWORD length = GetModuleFileNameW(nullptr, process, 32768);
    if (!length || length >= 32768)
        return;
    const wchar_t* name = std::wcsrchr(process, L'\\');
    if (!name || _wcsicmp(name + 1, L"eldenring.exe") != 0)
        return;
    if (InterlockedCompareExchange(&starting, 1, 0))
        return;
    HANDLE worker = CreateThread(nullptr, 0, diagnostic_worker, module, 0, nullptr);
    if (worker)
        CloseHandle(worker);
}

// Local test uses the exact detour and packet snapshot code, but a synthetic
// function in this DLL rather than a game address. No game process is opened.
__declspec(noinline) static std::uintptr_t synthetic_award(void* manager, const void* packet,
                                                           void* output, std::uint32_t flags) {
    volatile std::uintptr_t value = reinterpret_cast<std::uintptr_t>(manager);
    value += reinterpret_cast<std::uintptr_t>(output);
    value += static_cast<const AwardPacket*>(packet)->entries[0].quantity;
    return value + flags;
}
// Exported diagnostic routines below use synthetic fixtures for Windows ABI
// and hook checks. They do not need a game session.
extern "C" SL_API int SL_DiagnosticHookSelfTest() {
    if (InterlockedCompareExchange(&starting, 1, 0))
        return -1;
    AwardPacket invalid{};
    if (snapshot(nullptr, invalid) || snapshot(reinterpret_cast<void*>(1), invalid)) {
        InterlockedExchange(&starting, 0);
        return -5;
    }
    void* target = reinterpret_cast<void*>(&synthetic_award);
    int result = -2;
    MH_STATUS status = MH_Initialize();
    if (status == MH_OK) {
        status = MH_CreateHook(target, reinterpret_cast<void*>(&observe_award),
                               reinterpret_cast<void**>(&original_award));
        if (status == MH_OK && MH_EnableHook(target) == MH_OK) {
            AwardPacket packet{};
            packet.count = 1;
            packet.entries[0] = {0x400003e8, 3, 0, 0xffffffff};
            volatile AwardFn call = &synthetic_award;
            auto value = call(reinterpret_cast<void*>(10), &packet, reinterpret_cast<void*>(20), 4);
            Record record{};
            bool captured = pop_record(record);
            result = value == 37 && captured && record.valid && record.packet.count == 1 &&
                             record.packet.entries[0].quantity == 3 && record.flags == 4 &&
                             packet.entries[0].quantity == 3
                         ? 0
                         : -3;
            if (MH_DisableHook(target) != MH_OK)
                result = -4;
        }
        MH_Uninitialize();
    }
    original_award = nullptr;
    InterlockedExchange(&starting, 0);
    return result;
}

extern "C" SL_API int SL_DiagnosticFileMatches(const wchar_t* path) {
    if (!path)
        return 0;
    char digest[65]{};
    return hash_file(path, digest) && std::strcmp(digest, shared_loot::profile::sha256) == 0 ? 1
                                                                                             : 0;
}

namespace {
unsigned mock_grant_calls = 0;
unsigned mock_expected_quantity = 1;
bool mock_grant_valid = false;
std::uintptr_t mock_grant(void*, const void* packet, void* context, std::uint32_t flags) {
    ++mock_grant_calls;
    const auto& value = *static_cast<const AwardPacket*>(packet);
    const auto* notification = static_cast<const std::uint32_t*>(context);
    mock_grant_valid = inside && value.count == 1 && value.entries[0].item == 0x400050f0 &&
                       value.entries[0].quantity == mock_expected_quantity &&
                       value.entries[0].unknown == 0xffffffff &&
                       value.entries[0].gem == 0xffffffff && flags == 0 && notification[0] == 11 &&
                       notification[1] == 22 && notification[2] == 33;
    return 0;
}
} // namespace
extern "C" SL_API int SL_DiagnosticGrantSelfTest() {
    if (InterlockedCompareExchange(&starting, 1, 0))
        return -1;
    auto saved = original_award;
    original_award = &mock_grant;
    mock_grant_calls = 0;
    mock_grant_valid = false;
    mock_expected_quantity = 1;
    Record source{};
    source.valid = true;
    source.is_game = true;
    source.caller = 0x56441c;
    source.packet.count = 1;
    source.packet.entries[0] = {0x400050f0, 3, 0xffffffff, 0xffffffff};
    std::uint32_t context[3] = {11, 22, 33};
    InterlockedExchange(&grant_enabled, 1);
    InterlockedExchange(&grant_state, 1);
    InterlockedExchange64(&grant_armed_at, static_cast<LONG64>(GetTickCount64()));
    inside = true;
    source.flags = 4;
    bool excluded = !run_rowa_test(source, nullptr, context);
    source.flags = 0;
    bool issued = run_rowa_test(source, nullptr, context);
    bool blocked_repeat = !run_rowa_test(source, nullptr, context);
    inside = false;
    Record result{};
    bool captured = pop_record(result);
    bool ok = excluded && issued && blocked_repeat && mock_grant_calls == 1 && mock_grant_valid &&
              captured && result.kind == 1 && result.valid &&
              result.packet.entries[0].quantity == 1 && source.packet.entries[0].quantity == 3;
    original_award = saved;
    InterlockedExchange(&grant_enabled, 0);
    InterlockedExchange(&grant_state, 0);
    InterlockedExchange(&starting, 0);
    return ok ? 0 : -2;
}
extern "C" SL_API int SL_DiagnosticNetworkGrantSelfTest() {
    if (InterlockedCompareExchange(&starting, 1, 0))
        return -1;
    auto saved = original_award;
    original_award = &mock_grant;
    mock_grant_calls = 0;
    mock_grant_valid = false;
    mock_expected_quantity = 3;
    Record source{};
    source.valid = true;
    source.is_game = true;
    source.caller = 0x56441c;
    source.packet.count = 1;
    source.packet.entries[0] = {0x40005127, 1, 0xffffffff, 0xffffffff};
    std::uint32_t context[3] = {11, 22, 33};
    inside = true;
    source.flags = 4;
    bool npc_blocked = !issue_remote_rowa(source, nullptr, context, 3);
    source.flags = 0;
    source.is_game = false;
    bool mod_blocked = !issue_remote_rowa(source, nullptr, context, 3);
    source.is_game = true;
    bool bounds = !issue_remote_rowa(source, nullptr, context, 0) &&
                  !issue_remote_rowa(source, nullptr, context, 1000);
    bool issued = issue_remote_rowa(source, nullptr, context, 3);
    inside = false;
    Record result{};
    bool captured = pop_record(result);
    bool ok = npc_blocked && mod_blocked && bounds && issued && mock_grant_calls == 1 &&
              mock_grant_valid && captured && result.kind == 3 &&
              result.packet.entries[0].quantity == 3 &&
              result.packet.entries[0].item == 0x400050f0 &&
              source.packet.entries[0].item == 0x40005127;
    original_award = saved;
    InterlockedExchange(&starting, 0);
    return ok ? 0 : -2;
}
extern "C" SL_API int SL_FrameAndSteamAbiSelfTest() {
    steam_bridge.set_world(false);
    shared_loot::steam::Callback callback;
    int calls = 0;
    callback.owner = &calls;
    callback.bytes = 24;
    callback.handler = [](void* owner, void*) { ++*static_cast<int*>(owner); };
    auto** vtable = *reinterpret_cast<void***>(&callback);
    reinterpret_cast<void (*)(void*, void*)>(vtable[0])(&callback, nullptr);
    reinterpret_cast<void (*)(void*, void*, bool, std::uint64_t)>(vtable[1])(&callback, nullptr,
                                                                             false, 0);
    reinterpret_cast<void (*)(void*, void*, bool, std::uint64_t)>(vtable[1])(&callback, nullptr,
                                                                             true, 0);
    int size = reinterpret_cast<int (*)(void*)>(vtable[2])(&callback);
    auto before = InterlockedCompareExchange64(&frame_ticks, 0, 0);
    reinterpret_cast<void (*)(void*, const void*)>(frame_task.vtable[2])(&frame_task, nullptr);
    auto after = InterlockedCompareExchange64(&frame_ticks, 0, 0);
    return calls == 2 && size == 24 && sizeof(frame_task) == 64 && after == before + 1 &&
                   reinterpret_cast<void* (*)(void*)>(frame_task.vtable[0])(&frame_task) == nullptr
               ? 0
               : -1;
}
namespace {
AwardPacket expected_broad{};
bool broad_valid = false;
unsigned broad_calls = 0;
std::uintptr_t mock_broad(void*, const void* packet, void* context, std::uint32_t flags) {
    ++broad_calls;
    broad_valid =
        inside && flags == 0 && std::memcmp(packet, &expected_broad, sizeof(expected_broad)) == 0;
    for (unsigned i = 0; i < 8; ++i)
        if (static_cast<std::uint32_t*>(context)[i])
            broad_valid = false;
    return 0;
}
} // namespace
extern "C" SL_API int SL_BroadGrantSelfTest() {
    if (InterlockedCompareExchange(&starting, 1, 0))
        return -1;
    auto saved = original_award;
    original_award = &mock_broad;
    broad_calls = 0;
    broad_valid = false;
    expected_broad = {};
    expected_broad.count = 4;
    expected_broad.entries[0] = {0x400050f0, 3, 0xffffffff, 0xffffffff};
    expected_broad.entries[1] = {0x002e3bf0, 1, 0xffffffff, 10002};
    expected_broad.entries[2] = {0x1019f104, 1, 0xffffffff, 0xffffffff};
    expected_broad.entries[3] = {0x80002710, 1, 0xffffffff, 0xffffffff};
    bool unguarded = !issue_shared_packet(nullptr, expected_broad);
    inside = true;
    bool issued = issue_shared_packet(nullptr, expected_broad);
    inside = false;
    Record result{};
    bool captured = pop_record(result);
    bool ok = unguarded && issued && captured && broad_valid && broad_calls == 1 &&
              result.kind == 5 &&
              std::memcmp(&result.packet, &expected_broad, sizeof(expected_broad)) == 0;
    original_award = saved;
    InterlockedExchange(&starting, 0);
    return ok ? 0 : -2;
}

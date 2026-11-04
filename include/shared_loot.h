#pragma once
#include <stdint.h>

#if defined(_WIN32)
#if defined(SL_BUILD)
#define SL_API __declspec(dllexport)
#else
#define SL_API __declspec(dllimport)
#endif
#else
#define SL_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Diagnostic adapter boundary. No game memory is accessed by this API.
 * All calls for one context must occur on the same thread (future game thread).
 * The transport authenticates peer identities before passing messages here.
 * Item IDs are opaque resolved IDs; source classification belongs to the adapter.
 */
typedef struct SL_Context SL_Context;
enum SL_Source { SL_FLOOR = 1, SL_ENEMY = 2, SL_OTHER = 3 };
enum SL_Result {
    SL_OK = 0,
    SL_EMPTY = 1,
    SL_IGNORED = 2,
    SL_DUPLICATE = 3,
    SL_INVALID = -1,
    SL_FULL = -2,
    SL_GRANT_FAILED = -3
};
typedef struct SL_Event {
    uint64_t origin;
    uint64_t sequence;
    uint32_t item;
    uint32_t quantity;
    uint32_t source;
    uint32_t reserved;
} SL_Event;
/* A return value of 1 means the adapter successfully granted the complete item.
 * The callback may call SL_Capture: the suppression guard prevents rebroadcast.
 * It must not call other functions on the context or throw exceptions.
 */
typedef int (*SL_GrantFn)(void* user, uint32_t item, uint32_t quantity);
SL_API uint32_t SL_ApiVersion(void);
SL_API SL_Context* SL_Create(uint64_t local_peer);
SL_API void SL_Destroy(SL_Context* context);
/* Called AFTER a real local pickup succeeds; the collector already owns it. */
SL_API int SL_Capture(SL_Context*, uint32_t item, uint32_t quantity, uint32_t source);
SL_API int SL_NextOutgoing(SL_Context*, SL_Event* output);
/* Same event identity is ignored for this context's lifetime. Ordered, reliable
 * delivery is required per origin. Reconnect creates a fresh origin/session ID.
 */
SL_API int SL_Receive(SL_Context*, const SL_Event* event);
SL_API int SL_ApplyNext(SL_Context*, SL_GrantFn grant, void* user);
#ifdef __cplusplus
}
#endif

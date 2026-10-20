// Opt-in Dreamcast allocator trace. Wrappers only record fixed-size events;
// they never allocate or print while inside the allocator.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <reent.h>

namespace {
enum class EventKind : uint8_t { Malloc, Calloc, Realloc, New, Sbrk };
struct Event {
    EventKind kind;
    std::size_t requested;
    uintptr_t caller;
    uintptr_t result;
};
constexpr std::size_t kEventCapacity = 96;
constexpr std::size_t kLargeAllocation = 256u * 1024u;
constexpr std::size_t kLargeSbrk = 512u * 1024u;
Event events[kEventCapacity]{};
std::size_t event_count = 0;
std::size_t dropped_events = 0;

void record(EventKind kind, std::size_t requested, uintptr_t caller,
            uintptr_t result) {
    if (event_count == kEventCapacity) {
        ++dropped_events;
        return;
    }
    events[event_count++] = {kind, requested, caller, result};
}
}  // namespace

extern "C" {
void* __real__malloc_r(struct _reent*, std::size_t);
void* __real__calloc_r(struct _reent*, std::size_t, std::size_t);
void* __real__realloc_r(struct _reent*, void*, std::size_t);
void* __real__sbrk_r(struct _reent*, std::ptrdiff_t);
void* __real_malloc(std::size_t);
void* __real_calloc(std::size_t, std::size_t);
void* __real_realloc(void*, std::size_t);
void* __real__Znwj(std::size_t);

void* __wrap_malloc(std::size_t n) {
    void* p = __real_malloc(n);
    if (n >= kLargeAllocation)
        record(EventKind::Malloc, n,
               reinterpret_cast<uintptr_t>(__builtin_return_address(0)),
               reinterpret_cast<uintptr_t>(p));
    return p;
}

void* __wrap_calloc(std::size_t n, std::size_t size) {
    void* p = __real_calloc(n, size);
    const std::size_t total = size && n > SIZE_MAX / size
        ? SIZE_MAX : n * size;
    if (total >= kLargeAllocation)
        record(EventKind::Calloc, total,
               reinterpret_cast<uintptr_t>(__builtin_return_address(0)),
               reinterpret_cast<uintptr_t>(p));
    return p;
}

void* __wrap_realloc(void* old, std::size_t n) {
    void* p = __real_realloc(old, n);
    if (n >= kLargeAllocation)
        record(EventKind::Realloc, n,
               reinterpret_cast<uintptr_t>(__builtin_return_address(0)),
               reinterpret_cast<uintptr_t>(p));
    return p;
}

void* __wrap__Znwj(std::size_t n) {
    void* p = __real__Znwj(n);
    if (n >= kLargeAllocation)
        record(EventKind::New, n,
               reinterpret_cast<uintptr_t>(__builtin_return_address(0)),
               reinterpret_cast<uintptr_t>(p));
    return p;
}

void* __wrap__malloc_r(struct _reent* r, std::size_t n) {
    void* p = __real__malloc_r(r, n);
    if (n >= kLargeAllocation)
        record(EventKind::Malloc, n,
               reinterpret_cast<uintptr_t>(__builtin_return_address(0)),
               reinterpret_cast<uintptr_t>(p));
    return p;
}

void* __wrap__calloc_r(struct _reent* r, std::size_t n, std::size_t size) {
    void* p = __real__calloc_r(r, n, size);
    const std::size_t total = size && n > SIZE_MAX / size
        ? SIZE_MAX : n * size;
    if (total >= kLargeAllocation)
        record(EventKind::Calloc, total,
               reinterpret_cast<uintptr_t>(__builtin_return_address(0)),
               reinterpret_cast<uintptr_t>(p));
    return p;
}

void* __wrap__realloc_r(struct _reent* r, void* old, std::size_t n) {
    void* p = __real__realloc_r(r, old, n);
    if (n >= kLargeAllocation)
        record(EventKind::Realloc, n,
               reinterpret_cast<uintptr_t>(__builtin_return_address(0)),
               reinterpret_cast<uintptr_t>(p));
    return p;
}

void* __wrap__sbrk_r(struct _reent* r, std::ptrdiff_t n) {
    void* p = __real__sbrk_r(r, n);
    if (n >= static_cast<std::ptrdiff_t>(kLargeSbrk))
        record(EventKind::Sbrk, static_cast<std::size_t>(n),
               reinterpret_cast<uintptr_t>(__builtin_return_address(0)),
               reinterpret_cast<uintptr_t>(p));
    return p;
}

void sma2_memory_alloc_diag_dump() {
    for (std::size_t i = 0; i < event_count; ++i) {
        const Event& e = events[i];
        const char* kind = "?";
        switch (e.kind) {
            case EventKind::Malloc: kind = "malloc"; break;
            case EventKind::Calloc: kind = "calloc"; break;
            case EventKind::Realloc: kind = "realloc"; break;
            case EventKind::New: kind = "new"; break;
            case EventKind::Sbrk: kind = "sbrk"; break;
        }
        std::fprintf(stderr,
                     "[sma2-memory] event=%zu kind=%s bytes=%zu caller=%08lx result=%08lx\n",
                     i, kind, e.requested,
                     static_cast<unsigned long>(e.caller),
                     static_cast<unsigned long>(e.result));
    }
    std::fprintf(stderr, "[sma2-memory] events=%zu dropped=%zu\n",
                 event_count, dropped_events);
}
}  // extern "C"

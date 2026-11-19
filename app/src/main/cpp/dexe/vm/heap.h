#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace dexe::vm {

struct Class;
struct Monitor;
struct Object;
class Runtime;

// Header immediately preceding every allocated Java object or array in the heap.
// 48 bytes (a multiple of 16) so objects keep 16-byte alignment.
struct HeapHeader {
    HeapHeader* next = nullptr;
    HeapHeader* prev = nullptr;
    size_t size = 0;            // size of the object in bytes
    Monitor* monitor = nullptr; // the object's lock, created on first use. Kept here rather than in
                                // Object.shadow$_monitor_, which libcore reads as a lock word for hash codes.
    uint32_t flags = 0;
    uint32_t reserved[3] = {};

    static constexpr uint32_t kMarkBit = 1 << 0;
};
static_assert(sizeof(HeapHeader) % 16 == 0);

inline Object* header_to_object(HeapHeader* h) { return reinterpret_cast<Object*>(h + 1); }
inline HeapHeader* object_to_header(Object* obj) { return reinterpret_cast<HeapHeader*>(obj) - 1; }

// Non-moving mark-sweep heap. Allocation is thread-safe; collection is driven by Runtime::collect_garbage,
// which stops all other threads first.
class Heap {
public:
    // Setting the environment variable DEXE_GC_STRESS=<n> collects every n allocations and fills freed memory
    // with a poison pattern, so that missing GC roots show up as immediate failures instead of rare corruption.
    Heap();
    ~Heap();
    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;

    // Allocates `bytes` of zeroed memory. Never collects; call should_collect() first.
    Object* allocate(size_t bytes);
    // Whether an allocation of `bytes` should be preceded by a collection.
    bool should_collect(size_t bytes);

    // Marks from the runtime's roots and every thread's stack, processes references, and sweeps. Must be
    // called with all other threads stopped. Softly reachable referents are kept unless clear_soft. Returns the
    // cleared references that have a queue (including FinalizerReferences of objects to finalize).
    std::vector<Object*> collect(Runtime& rt, bool clear_soft);

    // Frees all heap memory at shutdown.
    void clear();

    // Calls fn(object, size in bytes) for every object, alive or not yet collected. Only with every other thread
    // stopped (Runtime::with_world_stopped), and fn must not allocate.
    template <typename F>
    void for_each_object(F&& fn) {
        std::lock_guard lock(heap_mutex_);
        for (HeapHeader* h = head_; h; h = h->next) fn(header_to_object(h), h->size);
    }

    size_t bytes_allocated() const { return bytes_allocated_; }
    size_t gc_count() const { return gc_count_; }

    // The heap limit, as Android's per-app limit (dalvik.vm.heapsize; Runtime.maxMemory()). Apps size their caches
    // from it, and allocations beyond it throw OutOfMemoryError.
    static constexpr size_t kMaxBytes = size_t(512) << 20;
    size_t max_bytes() const { return kMaxBytes; }
    bool fits(size_t bytes) const { return bytes_allocated_ + bytes <= kMaxBytes; }
    // The current heap size (Runtime.totalMemory()): what is allocated plus room until the next collection.
    size_t total_bytes() const { return std::min(kMaxBytes, std::max(gc_threshold_, bytes_allocated_)); }
    size_t free_bytes() const { return total_bytes() - std::min(total_bytes(), bytes_allocated_); }

private:
    HeapHeader* head_ = nullptr;
    size_t bytes_allocated_ = 0;
    size_t gc_threshold_ = 4 * 1024 * 1024;  // 4 MB initial threshold
    size_t total_allocations_ = 0;
    size_t gc_count_ = 0;
    size_t stress_interval_ = 0;  // DEXE_GC_STRESS: collect every n allocations (0 = off)
    std::mutex heap_mutex_;
};

}  // namespace dexe::vm
